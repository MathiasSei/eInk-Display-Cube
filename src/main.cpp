#include "config.h"
#include "display_ui.h"
#include "network_time.h"
#include <WiFi.h>
#include <Preferences.h>

Preferences prefs;

// Global instantiation declarations
size_t rtcPageCount = 0;
size_t rtcCurrentPageIndex = 0;
uint32_t rtcPageDelaySec = 45; 
uint32_t rtcTickCounter = 0;
uint32_t rtcFailCount = 0;
bool rtcHasValidData = false;
bool rtcIsFirstBoot = true;
bool rtcTriggerFullRefresh = true; 
char rtcErrorMessage[64] = "No Error"; 
char statusIcon = 'F'; 
const char* awsEndpoint = "https://lowbd437e3.execute-api.eu-north-1.amazonaws.com/v1/data";

void setup() {
    Serial.begin(115200);
    delay(1000); 
    
    Serial.println("\n--- ESP32-C3 Modular Boot Sequential Mode ---");
    loadStateFromFlash(); 

    analogSetAttenuation(ADC_11db);
    pinMode(BATTERY_PIN, INPUT);

    setenv("TZ", "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", 1);
    tzset();
    
    initDisplay();

    if (rtcIsFirstBoot) {
        rtcIsFirstBoot = false;
        rtcHasValidData = false;
        rtcCurrentPageIndex = 0;
        rtcTickCounter = 0;
        rtcFailCount = 0;
        rtcTriggerFullRefresh = true; 
        strlcpy(rtcErrorMessage, "None", sizeof(rtcErrorMessage));
        saveStateToFlash();
    }

    // Network Processing Conditional Logic Block
    if (!rtcHasValidData || rtcPageCount == 0) {
        if (rtcFailCount > 0) statusIcon = 'R'; 
        
        Serial.println("[System] Data Cache Empty. Connecting...");
        if (!connectWiFi()) {
            handleError("WiFi Link Timeout");
        }
        
        syncTime();
        statusIcon = 'F';
        
        updateDisplay("Fetching API...");
        if (rtcTriggerFullRefresh) { rtcTriggerFullRefresh = false; }

        if (!fetchAPIData()) {
            Serial.println("[ERROR] Core data fetch execution routine dropped!");
        }
        
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        
        rtcHasValidData = true;
        rtcCurrentPageIndex = 0; 
        rtcFailCount = 0;
        saveStateToFlash();
    }

    statusIcon = 'W'; 
    updateDisplay(""); 

    rtcCurrentPageIndex++;
    rtcTickCounter += 10; 

    if (rtcCurrentPageIndex >= rtcPageCount) {
        rtcCurrentPageIndex = 0; 
        rtcHasValidData = false; 
        rtcTriggerFullRefresh = true; 
    }

    saveStateToFlash(); 

    statusIcon = 'S';
    esp_sleep_enable_timer_wakeup(rtcPageDelaySec * 1000000ULL);
    Serial.flush();
    esp_deep_sleep_start();
}

void loop() {}

void loadStateFromFlash() {
    prefs.begin("dash_state", true); 
    rtcIsFirstBoot = prefs.getBool("firstBoot", true);
    rtcHasValidData = prefs.getBool("validData", false);
    rtcTriggerFullRefresh = prefs.getBool("fullRefresh", true); 
    rtcCurrentPageIndex = prefs.getUInt("pageIdx", 0);
    rtcPageCount = prefs.getUInt("pageCount", 0);
    rtcPageDelaySec = prefs.getUInt("delaySec", 45);
    rtcTickCounter = prefs.getUInt("tick", 0);
    rtcFailCount = prefs.getUInt("failCount", 0);
    prefs.getString("errMsg", rtcErrorMessage, sizeof(rtcErrorMessage));
    prefs.end();
}

void saveStateToFlash() {
    prefs.begin("dash_state", false); 
    prefs.putBool("firstBoot", rtcIsFirstBoot);
    prefs.putBool("validData", rtcHasValidData);
    prefs.putBool("fullRefresh", rtcTriggerFullRefresh); 
    prefs.putUInt("pageIdx", rtcCurrentPageIndex);
    prefs.putUInt("pageCount", rtcPageCount);
    prefs.putUInt("delaySec", rtcPageDelaySec);
    prefs.putUInt("tick", rtcTickCounter);
    prefs.putUInt("failCount", rtcFailCount);
    prefs.putString("errMsg", rtcErrorMessage);
    prefs.end();
}

void handleError(const char* conceptualError) {
    statusIcon = 'X'; rtcFailCount++; rtcHasValidData = false; rtcTriggerFullRefresh = true;
    strlcpy(rtcErrorMessage, conceptualError, sizeof(rtcErrorMessage));
    saveStateToFlash(); 
    updateDisplay("");
    esp_sleep_enable_timer_wakeup(30 * 1000000ULL);
    Serial.flush(); 
    esp_deep_sleep_start();
}