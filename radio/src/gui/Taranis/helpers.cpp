/*
 * Authors (alphabetical order)
 * - Andre Bernet <bernet.andre@gmail.com>
 * - Andreas Weitl
 * - Bertrand Songis <bsongis@gmail.com>
 * - Bryan J. Rentoul (Gruvin) <gruvin@gmail.com>
 * - Cameron Weeks <th9xer@gmail.com>
 * - Erez Raviv
 * - Gabriel Birkus
 * - Jean-Pierre Parisy
 * - Karl Szmutny
 * - Michael Blandford
 * - Michal Hlavinka
 * - Pat Mackenzie
 * - Philip Moss
 * - Rob Thomson
 * - Romolo Manfredini <romolo.manfredini@gmail.com>
 * - Thomas Husterer
 *
 * opentx is based on code named
 * gruvin9x by Bryan J. Rentoul: http://code.google.com/p/gruvin9x/,
 * er9x by Erez Raviv: http://code.google.com/p/er9x/,
 * and the original (and ongoing) project by
 * Thomas Husterer, th9x: http://code.google.com/p/th9x/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "../../opentx.h"

uint8_t switchToMix(uint8_t source)
{
  if (source <= 5*3)
    return MIXSRC_FIRST_SWITCH + (source-1) / 3;
  else if (source <= 17)
    return MIXSRC_SF;
  else if (source <= 20)
    return MIXSRC_SG;
  else
    return MIXSRC_SH;
}

int circularIncDec(int current, int inc, int min, int max, IsValueAvailable isValueAvailable)
{
  do {
    current += inc;
    if (current < min)
      current = max;
    else if (current > max)
      current = min;
    if (!isValueAvailable || isValueAvailable(current))
      return current;
  } while(1);
  return 0;
}

bool isInputAvailable(int input)
{
  for (int i=0; i<MAX_EXPOS; i++) {
    ExpoData * expo = expoAddress(i);
    if (!EXPO_VALID(expo))
      break;
    if (expo->chn == input)
      return true;
  }
  return false;
}

bool isSourceAvailable(int source)
{
  if (source>=MIXSRC_FIRST_INPUT && source<=MIXSRC_LAST_INPUT) {
    return isInputAvailable(source - MIXSRC_FIRST_INPUT);
  }

#if defined(LUA_MODEL_SCRIPTS)
  if (source>=MIXSRC_FIRST_LUA && source<=MIXSRC_LAST_LUA) {
    div_t qr = div(source-MIXSRC_FIRST_LUA, MAX_SCRIPT_OUTPUTS);
    return (qr.rem<scriptInputsOutputs[qr.quot].outputsCount);
  }
#else
  if (source>=MIXSRC_FIRST_LUA && source<=MIXSRC_LAST_LUA)
    return false;
#endif

  if (source>=MIXSRC_FIRST_POT && source<=MIXSRC_LAST_POT) {
    return IS_POT_AVAILABLE(POT1+source-MIXSRC_FIRST_POT);
  }

#if !defined(HELI)
  if (source>=MIXSRC_CYC1 && source<=MIXSRC_CYC3)
    return false;
#endif

  if (source>=MIXSRC_CH1 && source<=MIXSRC_LAST_CH) {
    uint8_t destCh = source-MIXSRC_CH1;
    for (uint8_t i = 0; i < MAX_MIXERS; i++) {
      MixData *md = mixAddress(i);
      if (md->srcRaw == 0) return false;
      if (md->destCh==destCh) return true;
    }
    return false;
  }

  if (source>=MIXSRC_SW1 && source<=MIXSRC_LAST_LOGICAL_SWITCH) {
    LogicalSwitchData * cs = lswAddress(source-MIXSRC_SW1);
    return (cs->func != LS_FUNC_NONE);
  }

#if !defined(GVARS)
  if (source>=MIXSRC_GVAR1 && source<=MIXSRC_LAST_GVAR)
    return false;
#endif

  if (source>=MIXSRC_RESERVE1 && source<=MIXSRC_RESERVE5)
    return false;

  if (source>=MIXSRC_FIRST_TELEM && source<=MIXSRC_LAST_TELEM) {
    div_t qr = div(source-MIXSRC_FIRST_TELEM, 3);
    if (qr.rem == 0)
      return isTelemetryFieldAvailable(qr.quot);
    else
      return isTelemetryFieldComparisonAvailable(qr.quot);
  }

  return true;
}

bool isSourceAvailableInCustomSwitches(int source)
{
  bool result = isSourceAvailable(source);

#if defined(FRSKY)
  if (result && source>=MIXSRC_FIRST_TELEM && source<=MIXSRC_LAST_TELEM) {
    div_t qr = div(source-MIXSRC_FIRST_TELEM, 3);
    result = isTelemetryFieldComparisonAvailable(qr.quot);
  }
#endif

  return result;
}

bool isInputSourceAvailable(int source)
{
  if (source>=MIXSRC_FIRST_POT && source<=MIXSRC_LAST_POT) {
    return IS_POT_AVAILABLE(POT1+source-MIXSRC_FIRST_POT);
  }

  if (source>=MIXSRC_Rud && source<=MIXSRC_MAX)
    return true;

  if (source>=MIXSRC_TrimRud && source<MIXSRC_SW1)
    return true;

  if (source>=MIXSRC_FIRST_CH && source<=MIXSRC_LAST_CH)
    return true;

  if (source>=MIXSRC_FIRST_TRAINER && source<=MIXSRC_LAST_TRAINER)
    return true;

  if (source>=MIXSRC_FIRST_TELEM && source<=MIXSRC_LAST_TELEM) {
    div_t qr = div(source-MIXSRC_FIRST_TELEM, 3);
    return isTelemetryFieldAvailable(qr.quot) && isTelemetryFieldComparisonAvailable(qr.quot);
  }

  return false;
}

enum SwitchContext
{
  LogicalSwitchesContext,
  ModelCustomFunctionsContext,
  GeneralCustomFunctionsContext,
  TimersContext,
  MixesContext
};

bool isSwitchAvailable(int swtch, SwitchContext context)
{
  uint32_t index = switchInfo(abs(swtch)).quot;

  if (swtch < 0) {
    if (!IS_3POS(index))
      return false;
    if (swtch == -SWSRC_ON || swtch == -SWSRC_One) {
      return false;
    }
    swtch = -swtch;
  }

#if defined(REV9E)
  if (swtch >= SWSRC_SA0 && swtch <= SWSRC_SR2) {
    if (!SWITCH_EXISTS(index))
      return false;
    if ((swtch-SWSRC_SA1)%3 == 0)
      return IS_3POS(index);
  }
#else
  if (swtch == SWSRC_SA1 || swtch == SWSRC_SB1 || swtch == SWSRC_SC1 || swtch == SWSRC_SD1 || swtch == SWSRC_SE1 || swtch == SWSRC_SG1) {
    return IS_3POS(index);
  }
#endif

  if (swtch >= SWSRC_FIRST_MULTIPOS_SWITCH && swtch <= SWSRC_LAST_MULTIPOS_SWITCH) {
    int index = (swtch - SWSRC_FIRST_MULTIPOS_SWITCH) / XPOTS_MULTIPOS_COUNT;
    if (IS_POT_MULTIPOS(POT1+index)) {
      StepsCalibData * calib = (StepsCalibData *) &g_eeGeneral.calib[POT1+index];
      return (calib->count >= ((swtch - SWSRC_FIRST_MULTIPOS_SWITCH) % XPOTS_MULTIPOS_COUNT));
    }
    else {
      return false;
    }
  }

  if (swtch >= SWSRC_FIRST_LOGICAL_SWITCH && swtch <= SWSRC_LAST_LOGICAL_SWITCH) {
    if (context == GeneralCustomFunctionsContext) {
      return false;
    }
    else if (context != LogicalSwitchesContext) {
      LogicalSwitchData * cs = lswAddress(swtch-SWSRC_FIRST_LOGICAL_SWITCH);
      return (cs->func != LS_FUNC_NONE);
    }
  }

  if (context != ModelCustomFunctionsContext && context != GeneralCustomFunctionsContext && (swtch == SWSRC_ON || swtch == SWSRC_One)) {
    return false;
  }

  if (swtch >= SWSRC_FIRST_FLIGHT_MODE && swtch <= SWSRC_LAST_FLIGHT_MODE) {
    if (context == MixesContext || context == GeneralCustomFunctionsContext) {
      return false;
    }
    else {
      swtch -= SWSRC_FIRST_FLIGHT_MODE;
      if (swtch == 0) {
        return true;
      }
      FlightModeData * fm = flightModeAddress(swtch);
      return (fm->swtch != SWSRC_NONE);
    }
  }

  return true;
}

bool isSwitchAvailableInLogicalSwitches(int swtch)
{
  return isSwitchAvailable(swtch, LogicalSwitchesContext);
}

bool isSwitchAvailableInCustomFunctions(int swtch)
{
  if (g_menuStack[g_menuStackPtr] == menuModelCustomFunctions)
    return isSwitchAvailable(swtch, ModelCustomFunctionsContext);
  else
    return isSwitchAvailable(swtch, GeneralCustomFunctionsContext);
}

bool isSwitchAvailableInMixes(int swtch)
{
  return isSwitchAvailable(swtch, MixesContext);
}

bool isSwitchAvailableInTimers(int swtch)
{
  if (swtch >= 0) {
    if (swtch < TMRMODE_COUNT)
      return true;
    else
      swtch -= TMRMODE_COUNT-1;
  }
  else {
    if (swtch > -TMRMODE_COUNT)
      return false;
    else
      swtch += TMRMODE_COUNT-1;
  }

  return isSwitchAvailable(swtch, TimersContext);
}

bool isThrottleSourceAvailable(int source)
{
  if (source == THROTTLE_SOURCE_S3 && !IS_POT_AVAILABLE(POT3))
    return false;
  else
    return true;
}

bool isLogicalSwitchFunctionAvailable(int function)
{
  return function != LS_FUNC_RANGE;
}

bool isAssignableFunctionAvailable(int function)
{
  bool modelFunctions = (g_menuStack[g_menuStackPtr] == menuModelCustomFunctions);

  switch (function) {
    case FUNC_OVERRIDE_CHANNEL:
#if defined(OVERRIDE_CHANNEL_FUNCTION)
      return modelFunctions;
#else
      return false;
#endif
    case FUNC_ADJUST_GVAR:
#if defined(GVARS)
      return modelFunctions;
#else
      return false;
#endif
#if !defined(HAPTIC)
    case FUNC_HAPTIC:
#endif
    case FUNC_PLAY_DIFF:
#if !defined(DANGEROUS_MODULE_FUNCTIONS)
    case FUNC_RANGECHECK:
    case FUNC_BIND:
    case FUNC_MODULE_OFF:
#endif
#if !defined(LUA)
    case FUNC_PLAY_SCRIPT:
#endif
    case FUNC_RESERVE5:
      return false;

    default:
      return true;
  }
}

bool isModuleAvailable(int module)
{
  return true;
}

bool modelHasNotes()
{
  char filename[sizeof(MODELS_PATH)+1+sizeof(g_model.header.name)+sizeof(TEXT_EXT)] = MODELS_PATH "/";
  char *buf = strcat_modelname(&filename[sizeof(MODELS_PATH)], g_eeGeneral.currModel);
  strcpy(buf, TEXT_EXT);
  return isFileAvailable(filename);
}

int getFirstAvailableSource(int min, int max, bool (*func)(int))
{
  int retval = MIXSRC_NONE;
  for (int i = min; i <= max; i++) {
    if ((*func)(i)) {
      retval = i;
      break;
    }
  }
  return retval;
}
