#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <time.h>
#include <Preferences.h>
#include <esp_wifi.h>

// --- Font Inclusions ---
#include <Fonts/FreeSans9pt7b.h>       
#include <Fonts/FreeSansBold9pt7b.h>   
#include <Fonts/FreeSans12pt7b.h>     
#include <Fonts/FreeSansBold12pt7b.h> 

// --- Pin Definitions ---
#define EINK_SCL   4   
#define EINK_SDA   6   
#define EINK_CS    10  
#define EINK_DC    2   
#define EINK_RES   3   
#define EINK_BUSY  1   

// --- Display Selector ---
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(EINK_CS, EINK_DC, EINK_RES, EINK_BUSY)
);

Preferences prefs;

// Global runtime variables
size_t rtcPageCount = 0;
size_t rtcCurrentPageIndex = 0;
uint32_t rtcPageDelaySec = 45; 
uint32_t rtcTickCounter = 0;
uint32_t rtcFailCount = 0;
bool rtcHasValidData = false;
bool rtcIsFirstBoot = true;
bool rtcTriggerFullRefresh = true; // New Flag: Controls Full vs Partial refresh states
char rtcErrorMessage[64] = "No Error"; 

char statusIcon = 'F'; 
const char* awsEndpoint = "https://lowbd437e3.execute-api.eu-north-1.amazonaws.com/v1/data";

// --- Forward Declarations ---
void updateDisplay(const char* stepMsg);
void drawLayout(const char* stepMsg);
void handleError(const char* conceptualError);
void syncTime();
bool fetchAPIData();
void loadStateFromFlash();
void saveStateToFlash();

void setup() {
    Serial.begin(115200);
    delay(1000); 
    
    Serial.println("\n--- ESP32-C3 Wakeup / Flash Memory Mode ---");
    loadStateFromFlash(); 
    
    // Instantly force local Oslo rules on hardware wake
    setenv("TZ", "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", 1);
    tzset();
    
    // Initialize Display
    SPI.begin(EINK_SCL, -1, EINK_SDA, EINK_CS); 
    display.init(115200, rtcIsFirstBoot, 10, false);
    display.setRotation(3); 

    if (rtcIsFirstBoot) {
        rtcIsFirstBoot = false;
        rtcHasValidData = false;
        rtcCurrentPageIndex = 0;
        rtcTickCounter = 0;
        rtcFailCount = 0;
        rtcTriggerFullRefresh = true; // Force crisp clean on initial power-on
        strlcpy(rtcErrorMessage, "None", sizeof(rtcErrorMessage));
        saveStateToFlash();
    }

    // Network Sync Block
    if (!rtcHasValidData || rtcPageCount == 0) {
        if (rtcFailCount > 0) statusIcon = 'R'; 
        
        Serial.println("[Network] Cache empty. Activating Robust Wi-Fi Stack...");
        
        WiFi.mode(WIFI_STA);
        esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B); // Long range configuration
        WiFi.begin(LOCAL_SSID, LOCAL_PASS);
        esp_wifi_set_ps(WIFI_PS_NONE); // Full radio power alert mode
        WiFi.setTxPower(WIFI_POWER_15dBm); 

        int retry = 0;
        while (WiFi.status() != WL_CONNECTED && retry++ < 40) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() != WL_CONNECTED) {
            handleError("WiFi Link Timeout");
        }
        
        syncTime();

        statusIcon = 'F';
        
        // This display update uses the flag status determined at the end of the previous cycle
        updateDisplay("Fetching API...");
        
        // Once a full clean refresh runs successfully, reset the flag for subsequent partials
        if (rtcTriggerFullRefresh) {
            rtcTriggerFullRefresh = false; 
        }

        if (!fetchAPIData()) {
            Serial.println("[ERROR] API data fetch failed!");
        }
        
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        
        rtcHasValidData = true;
        rtcCurrentPageIndex = 0; 
        rtcFailCount = 0;
        saveStateToFlash();
    }

    statusIcon = 'W'; 
    Serial.printf("[UI] Rendering Page %d/%d...\n", (int)rtcCurrentPageIndex + 1, (int)rtcPageCount);
    
    // Render current layout out of flash storage memory arrays
    updateDisplay(""); 

    // Move pointers forward for the next deep sleep loop sequence
    rtcCurrentPageIndex++;
    rtcTickCounter += 10; 

    // CRITICAL REFRESH LOGIC BOUNDARY:
    // Check if we just completed rendering the final available dashboard page
    if (rtcCurrentPageIndex >= rtcPageCount) {
        Serial.println("[Paging] Last page complete. Invalidating cache and queueing full refresh flag.");
        rtcCurrentPageIndex = 0; 
        rtcHasValidData = false; 
        rtcTriggerFullRefresh = true; // Flag tells the system to execute a full clean on next loop wake
    }

    saveStateToFlash(); 

    statusIcon = 'S';
    Serial.printf("[DEEP SLEEP] Sleeping for %d seconds...\n\n", rtcPageDelaySec);
    esp_sleep_enable_timer_wakeup(rtcPageDelaySec * 1000000ULL);
    Serial.flush();
    esp_deep_sleep_start();
}

void loop() {}

// --- Flash Read/Write Helpers ---
void loadStateFromFlash() {
    prefs.begin("dash_state", true); 
    rtcIsFirstBoot = prefs.getBool("firstBoot", true);
    rtcHasValidData = prefs.getBool("validData", false);
    rtcTriggerFullRefresh = prefs.getBool("fullRefresh", true); // Pull full refresh state
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
    prefs.putBool("fullRefresh", rtcTriggerFullRefresh); // Save full refresh state
    prefs.putUInt("pageIdx", rtcCurrentPageIndex);
    prefs.putUInt("pageCount", rtcPageCount);
    prefs.putUInt("delaySec", rtcPageDelaySec);
    prefs.putUInt("tick", rtcTickCounter);
    prefs.putUInt("failCount", rtcFailCount);
    prefs.putString("errMsg", rtcErrorMessage);
    prefs.end();
}

// --- Display Logic ---
void drawLayout(const char* stepMsg) {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    
    // --- Top Bar Layout (Upgraded to Size 2) ---
    display.setFont(&FreeSansBold9pt7b); // Changed from FreeSans9pt7b
    
    // 1. Time string aligned to the far LEFT
    struct tm timeinfo;
    char timeStr[6] = "--:--";
    if (getLocalTime(&timeinfo) && timeinfo.tm_year >= 120) { 
        strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    }
    display.setCursor(5, 23); // Lowered from 20 to account for font height
    display.print(timeStr);

    // 2. Pure page counter aligned in the MIDDLE
    char midPageStr[16];
    snprintf(midPageStr, sizeof(midPageStr), "%d/%d", (int)rtcCurrentPageIndex + 1, (int)rtcPageCount);
    // Adjusted from 88 to 82 because Size 2 characters are wider
    display.setCursor(82, 23); 
    display.print(midPageStr);

    // 3. Battery string aligned to the far RIGHT
    char rightBatStr[17] = "N/A%";
    // Adjusted from width - 48 to width - 60 to prevent the text from clipping the right edge
    display.setCursor(display.width() - 60, 23);
    display.print(rightBatStr);

    // Lowered the divider line from 30 to 34 to accommodate the larger font footprint
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

    // --- Dynamic Content Sizing Pipeline ---
    prefs.begin("dash_pages", true);
    char pageKey[16];
    snprintf(pageKey, sizeof(pageKey), "p_%d_cnt", (int)rtcCurrentPageIndex);
    int itemsInPage = prefs.getInt(pageKey, 0);

    // Shifted core content starting point down from 65 to 72 to keep away from the lower divider line
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
        Serial.println("[Display] Executing heavy, anti-ghosting FULL refresh cycle...");
        display.firstPage();
        do { drawLayout(stepMsg); } while (display.nextPage());
    } else {
        Serial.println("[Display] Executing quick PARTIAL window refresh...");
        // Keeping boundaries set to maximum display width/height dimensions 
        // ensures the newly positioned top bar artifacts refresh smoothly.
        display.setPartialWindow(0, 0, display.width(), display.height());
        display.firstPage();
        do { drawLayout(stepMsg); } while (display.nextPage());
    }
}

void syncTime() {
    Serial.println("[NTP] Directly initializing Oslo Timezone with Auto DST...");
    configTzTime("CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", "no.pool.ntp.org", "pool.ntp.org");
    
    struct tm timeinfo;
    int retry = 0;
    while ((!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) && retry++ < 20) { 
        delay(500); 
        Serial.print("."); 
    }
    Serial.println();
}

bool fetchAPIData() {
    WiFiClientSecure client;
    client.setInsecure(); 
    HTTPClient http;
    
    http.begin(client, awsEndpoint);
    http.addHeader("X-API-Key", AWS_API_KEY);
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        char errBuf[48]; snprintf(errBuf, sizeof(errBuf), "HTTP Error: %d", httpCode);
        http.end(); handleError(errBuf); return false;
    }

    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        char errBuf[48]; snprintf(errBuf, sizeof(errBuf), "JSON Err: %s", error.c_str());
        http.end(); handleError(errBuf); return false;
    }

    if (doc["pageDelaySec"]) {
        rtcPageDelaySec = doc["pageDelaySec"].as<uint32_t>();
    }

    JsonArray pagesArray = doc["pages"].as<JsonArray>();
    rtcPageCount = 0;

    prefs.begin("dash_pages", false);
    prefs.clear();

    for (JsonObject p : pagesArray) {
        if (rtcPageCount >= 10) break; 

        JsonArray itemsArray = p["items"].as<JsonArray>();
        int itemCount = 0;

        for (JsonObject item : itemsArray) {
            if (itemCount >= 3) break; 

            const char* k = item["key"] | "";
            const char* v = item["value"] | "";
            int s = item["size"] | 2; 

            char storeKey[24];
            snprintf(storeKey, sizeof(storeKey), "p_%d_k_%d", (int)rtcPageCount, itemCount);
            prefs.putString(storeKey, k);

            snprintf(storeKey, sizeof(storeKey), "p_%d_v_%d", (int)rtcPageCount, itemCount);
            prefs.putString(storeKey, v);

            snprintf(storeKey, sizeof(storeKey), "p_%d_s_%d", (int)rtcPageCount, itemCount);
            prefs.putInt(storeKey, s);

            itemCount++;
        }
        
        char countKey[16];
        snprintf(countKey, sizeof(countKey), "p_%d_cnt", (int)rtcPageCount);
        prefs.putInt(countKey, itemCount);
        
        rtcPageCount++;
    }
    prefs.end();

    http.end();
    return (rtcPageCount > 0);
}

void handleError(const char* conceptualError) {
    statusIcon = 'X';
    rtcFailCount++;
    rtcHasValidData = false;
    rtcTriggerFullRefresh = true; // Clean out artifacts when a hard error drops
    strlcpy(rtcErrorMessage, conceptualError, sizeof(rtcErrorMessage));
    saveStateToFlash();
    updateDisplay("");
    esp_sleep_enable_timer_wakeup(30 * 1000000ULL);
    Serial.flush();
    esp_deep_sleep_start();
}