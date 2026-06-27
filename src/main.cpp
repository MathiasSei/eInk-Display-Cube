#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <time.h>
#include <Preferences.h>

// --- Font Inclusions for Variable Layout Sizing ---
#include <Fonts/FreeSans9pt7b.h>       // Size 1 (Small)
#include <Fonts/FreeSans12pt7b.h>     // Size 2 (Medium - Default)
#include <Fonts/FreeSansBold12pt7b.h> // Size 3 (Large/Bold)

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
        strlcpy(rtcErrorMessage, "None", sizeof(rtcErrorMessage));
        saveStateToFlash();
    }

    // Network Sync Block
    if (!rtcHasValidData || rtcPageCount == 0) {
        if (rtcFailCount > 0) statusIcon = 'R'; 
        
        WiFi.mode(WIFI_STA);
        WiFi.begin(LOCAL_SSID, LOCAL_PASS);
        WiFi.setTxPower(WIFI_POWER_15dBm); 

        int retry = 0;
        while (WiFi.status() != WL_CONNECTED && retry++ < 30) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() != WL_CONNECTED) {
            handleError("WiFi Disconnected");
        }
        
        syncTime();

        statusIcon = 'F';
        updateDisplay("Fetching API...");
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
    updateDisplay(""); // Passing empty string since step string logic has been deprecated

    // Increment pagination tracking
    rtcCurrentPageIndex++;
    rtcTickCounter += 10; 

    if (rtcCurrentPageIndex >= rtcPageCount) {
        rtcCurrentPageIndex = 0; 
        rtcHasValidData = false; 
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
    
    // --- Top Bar Layout ---
    display.setFont(&FreeSans9pt7b);
    struct tm timeinfo;
    char timeStr[6] = "--:--";
    if (getLocalTime(&timeinfo) && timeinfo.tm_year >= 120) { 
        strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    }
    display.setCursor(5, 20);
    display.print(timeStr);

    // Dynamic Pagination Counter inside top header string bar
    char topRightStr[32];
    snprintf(topRightStr, sizeof(topRightStr), "[%d/%d] N/A%% [%c]", (int)rtcCurrentPageIndex + 1, (int)rtcPageCount, statusIcon);
    display.setCursor(display.width() - 120, 20);
    display.print(topRightStr);

    display.drawFastHLine(0, 30, display.width(), GxEPD_BLACK);

    if (statusIcon == 'X') {
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(5, 65);
        display.print("Error Occurred:");
        display.setFont(&FreeSans9pt7b);
        display.setCursor(5, 105);
        display.print(rtcErrorMessage);
        display.setCursor(5, 145);
        display.printf("Retrying in 30s... (%d)", (int)rtcFailCount);
        return;
    }

    // --- Dynamic Content Sizing Pipeline ---
    prefs.begin("dash_pages", true);
    char pageKey[16];
    snprintf(pageKey, sizeof(pageKey), "p_%d_cnt", (int)rtcCurrentPageIndex);
    int itemsInPage = prefs.getInt(pageKey, 0);

    int yStart = 65; // Shift layout up slightly since we cleared the placeholder row
    for (int i = 0; i < itemsInPage; i++) {
        char kStr[32] = "";
        char vStr[64] = "";
        
        snprintf(pageKey, sizeof(pageKey), "p_%d_k_%d", (int)rtcCurrentPageIndex, i);
        prefs.getString(pageKey, kStr, sizeof(kStr));
        
        snprintf(pageKey, sizeof(pageKey), "p_%d_v_%d", (int)rtcCurrentPageIndex, i);
        prefs.getString(pageKey, vStr, sizeof(vStr));

        snprintf(pageKey, sizeof(pageKey), "p_%d_s_%d", (int)rtcCurrentPageIndex, i);
        int fontSize = prefs.getInt(pageKey, 2); // Fallback defaults to medium template layouts

        // Assign active font layer pointer dynamically based on API parameter
        if (fontSize == 1) {
            display.setFont(&FreeSans9pt7b);
            yStart += 5; // Balanced line space offset adjustment
        } else if (fontSize == 3) {
            display.setFont(&FreeSansBold12pt7b);
            yStart += 12;
        } else {
            display.setFont(&FreeSans12pt7b); // Default size 2
            yStart += 10;
        }

        display.setCursor(5, yStart);
        display.printf("%s: %s", kStr, vStr);
        
        // Incremental vertical drop based on typographic scaling density bounds
        yStart += (fontSize == 1) ? 22 : (fontSize == 3) ? 35 : 28;
    }
    prefs.end();
}

void updateDisplay(const char* stepMsg) {
    if (rtcTickCounter == 0 && rtcCurrentPageIndex == 0) {
        display.firstPage();
        do { drawLayout(stepMsg); } while (display.nextPage());
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
        display.firstPage();
        do { drawLayout(stepMsg); } while (display.nextPage());
    }
}

void syncTime() {
    Serial.println("[NTP] Configuring Oslo Timezone parameters...");
    
    // 1. Set the environment time zone string FIRST
    // CET-1CEST means standard time is UTC+1, daylight savings (CEST) is UTC+2
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    // 2. Start the network clock sync request
    configTime(0, 0, "no.pool.ntp.org", "pool.ntp.org");
    
    struct tm timeinfo;
    int retry = 0;
    
    // 3. Block and wait until NTP securely fetches the true calendar time
    while ((!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) && retry++ < 20) { 
        delay(500); 
        Serial.print("."); 
    }
    Serial.println();
    
    if (getLocalTime(&timeinfo) && timeinfo.tm_year >= 120) {
        char debugTime[32];
        strftime(debugTime, sizeof(debugTime), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Serial.printf("[NTP] Oslo Clock Sync Successful: %s\n", debugTime);
    } else {
        Serial.println("[NTP] ERROR: Time sync timed out. Clock might be inaccurate.");
    }
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
            int s = item["size"] | 2; // Grab sizing integer parameter from json file structure path

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
    strlcpy(rtcErrorMessage, conceptualError, sizeof(rtcErrorMessage));
    saveStateToFlash();
    updateDisplay("");
    esp_sleep_enable_timer_wakeup(30 * 1000000ULL);
    Serial.flush();
    esp_deep_sleep_start();
}