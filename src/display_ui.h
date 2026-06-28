#pragma once
#include "config.h"
#include <GxEPD2_BW.h>

// Expose the display object to other compilation units
extern GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display;

void initDisplay();
void drawLayout(const char* stepMsg);
void updateDisplay(const char* stepMsg);
int getBatteryPercentage();