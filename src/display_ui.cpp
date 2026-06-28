#include "display_ui.h"
#include <SPI.h>
#include <Fonts/FreeSans9pt7b.h>       
#include <Fonts/FreeSans12pt7b.h>     
#include <Fonts/FreeSansBold12pt7b.h> 
#include <Preferences.h>

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(EINK_CS, EINK_DC, EINK_RES, EINK_BUSY)
);

extern Preferences prefs;

void initDisplay() {
    SPI.begin(EINK_SCL, -1, EINK_SDA, EINK_CS); 
    display.init(115200, rtcIsFirstBoot, 10, false);
    display.setRotation(3); 
}

int getBatteryPercentage() {
    uint32_t rawSum = 0;
    for(int i = 0; i < 10; i++) {
        rawSum += analogReadMilliVolts(BATTERY_PIN);
        delay(5);
    }
    double measuredMv = rawSum / 10.0;
    double batV = (measuredMv * 2.0) / 1000.0; 

    int pct = (int)((batV - 3.3) / (4.2 - 3.3) * 100.0);
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    return pct;
}

void drawLayout(const char* stepMsg) {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    
    display.setFont(&FreeSans12pt7b); 
    
    struct tm timeinfo;
    char timeStr[6] = "--:--";
    if (getLocalTime(&timeinfo) && timeinfo.tm_year >= 120) { 
        strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    }
    display.setCursor(5, 23); 
    display.print(timeStr);

    char midPageStr[16];
    snprintf(midPageStr, sizeof(midPageStr), "%d/%d", (int)rtcCurrentPageIndex + 1, (int)rtcPageCount);
    display.setCursor(82, 23); 
    display.print(midPageStr);

    char rightBatStr[16];
    snprintf(rightBatStr, sizeof(rightBatStr), "%d%%", getBatteryPercentage());
    display.setCursor(display.width() - 60, 23);
    display.print(rightBatStr);

    display.drawFastHLine(0, 34, display.width(), GxEPD_BLACK);

    if (statusIcon == 'X') {
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(5, 72);
        display.print("Error Occurred:");
        display.setFont(&FreeSans9pt7b);
        display.setCursor(5, 110);
        display.print(rtcErrorMessage);
        display.setCursor(5, 150);
        display.printf("Retrying in 30s... (%d)", (int)rtcFailCount);
        return;
    }

    prefs.begin("dash_pages", true);
    char pageKey[16];
    snprintf(pageKey, sizeof(pageKey), "p_%d_cnt", (int)rtcCurrentPageIndex);
    int itemsInPage = prefs.getInt(pageKey, 0);

    int yStart = 72; 
    for (int i = 0; i < itemsInPage; i++) {
        char kStr[32] = "";
        char vStr[64] = "";
        
        snprintf(pageKey, sizeof(pageKey), "p_%d_k_%d", (int)rtcCurrentPageIndex, i);
        prefs.getString(pageKey, kStr, sizeof(kStr));
        
        snprintf(pageKey, sizeof(pageKey), "p_%d_v_%d", (int)rtcCurrentPageIndex, i);
        prefs.getString(pageKey, vStr, sizeof(vStr));

        snprintf(pageKey, sizeof(pageKey), "p_%d_s_%d", (int)rtcCurrentPageIndex, i);
        int fontSize = prefs.getInt(pageKey, 2); 

        if (fontSize == 1) {
            display.setFont(&FreeSans9pt7b);
            yStart += 5; 
        } else if (fontSize == 3) {
            display.setFont(&FreeSansBold12pt7b);
            yStart += 12;
        } else {
            display.setFont(&FreeSans12pt7b); 
            yStart += 10;
        }

        display.setCursor(5, yStart);
        display.printf("%s: %s", kStr, vStr);
        yStart += (fontSize == 1) ? 22 : (fontSize == 3) ? 35 : 28;
    }
    prefs.end();
}

void updateDisplay(const char* stepMsg) {
    if (rtcTriggerFullRefresh) {
        display.firstPage();
        do { drawLayout(stepMsg); } while (display.nextPage());
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
        display.firstPage();
        do { drawLayout(stepMsg); } while (display.nextPage());
    }
}