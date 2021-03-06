// cLcd.h
#pragma once
//{{{  includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <array>

#include "stm32f7xx.h"
#include "stm32f7xx_hal.h"
#include "stm32746g_lcd.h"
//}}}
//{{{
#ifdef __cplusplus
  extern "C" {
#endif
//}}}

class cLcd {
public:
  cLcd (int displayLines) : mDisplayLines (displayLines) {}
  //{{{
  void init() {

    BSP_LCD_Init();

    BSP_LCD_LayerDefaultInit (0, LCD_FB_START_ADDRESS);
    BSP_LCD_SelectLayer(0);
    BSP_LCD_Clear (LCD_COLOR_BLACK);
    BSP_LCD_SetLayerVisible (0, ENABLE);
    BSP_LCD_SetTransparency (0, 255);

    BSP_LCD_LayerDefaultInit (1, LCD_FB_START_ADDRESS + (BSP_LCD_GetXSize() * BSP_LCD_GetYSize() * 4));
    BSP_LCD_SelectLayer(1);
    BSP_LCD_Clear (LCD_COLOR_BLACK);
    BSP_LCD_SetLayerVisible (1, ENABLE);
    BSP_LCD_SetTransparency (1, 0);

    BSP_LCD_DisplayOn();
    }
  //}}}
  //{{{
  void show (const std::string& title) {

    BSP_LCD_SelectLayer (mLayer);
    BSP_LCD_Clear (LCD_COLOR_BLACK);

    uint32_t wait = 20 - (HAL_GetTick() % 20);
    HAL_Delay (wait);
    mTick = HAL_GetTick();

    char str1[40];
    sprintf (str1, "%s %d %d", title.c_str(), (int)mTick, (int)wait);
    BSP_LCD_SetTextColor (LCD_COLOR_WHITE);
    BSP_LCD_DisplayStringAtLine (0, str1);

    if (!BSP_PB_GetState (BUTTON_KEY))
      for (auto displayLine = 0u; (displayLine < mDebugLine) && ((int)displayLine < mDisplayLines); displayLine++) {
        int debugLine = ((int)mDebugLine < mDisplayLines) ?
          displayLine : (mDebugLine - mDisplayLines + displayLine - getScrollLines())  % kDebugMaxLines;

        BSP_LCD_SetTextColor (LCD_COLOR_WHITE);
        char tickStr[20];
        auto ticks = mLines[debugLine].mTicks;
        sprintf (tickStr, "%2d.%03d", (int)ticks / 1000, (int)ticks % 1000);
        BSP_LCD_DisplayStringAtLineColumn (1+displayLine, 0, tickStr);

        BSP_LCD_SetTextColor (mLines[debugLine].mColour);
        BSP_LCD_DisplayStringAtLineColumn (1+displayLine, 7, mLines[debugLine].mStr);
        }
    }
  //}}}
  //{{{
  void flip() {

    BSP_LCD_SetTransparency (mLayer, 255);
    mLayer = !mLayer;
    BSP_LCD_SetTransparency (mLayer, 0);
    }
  //}}}

  //{{{
  int getScrollScale() {
    return 4;
    }
  //}}}
  //{{{
  unsigned getScrollLines() {
    return mScroll / getScrollScale();
    }
  //}}}
  //{{{
  void incScrollValue (int inc) {

    mScroll += inc;

    if (mScroll < 0)
      mScroll = 0;
    else if (getScrollLines() >  mDebugLine - mDisplayLines)
      mScroll = (mDebugLine - mDisplayLines) * getScrollScale();
    }
  //}}}

  //{{{
  void debug (uint32_t colour, const char* format, ... ) {

    va_list args;
    va_start (args, format);
    auto str = (char*)malloc (40+1);
    vsnprintf (str, 40, format, args);
    va_end (args);

    mLines[mDebugLine % kDebugMaxLines].mStr = str;
    mLines[mDebugLine % kDebugMaxLines].mTicks = HAL_GetTick();
    mLines[mDebugLine % kDebugMaxLines].mColour = colour;
    mDebugLine++;
    }
  //}}}

private:
  static const int kDebugMaxLines = 200;
  //{{{
  class cDebugItem {
  public:
    cDebugItem() {}
    cDebugItem (char* str, uint32_t ticks, uint32_t colour) : mStr(str), mTicks(ticks), mColour(colour) {}
    //{{{
    ~cDebugItem() {
      free (mStr);
      }
    //}}}

    char* mStr = nullptr;
    uint32_t mTicks = 0;
    uint32_t mColour = 0;
    };
  //}}}

  int mLayer = 0;
  uint32_t mTick = 0;

  int mDisplayLines;
  unsigned mDebugLine = 0;
  std::array <cDebugItem,kDebugMaxLines> mLines;
  int mScroll = 0;
  };

//{{{
#ifdef __cplusplus
}
#endif
//}}}
