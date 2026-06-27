#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <SPI.h>
#include <time.h>
#include <Preferences.h> // Include Espressif's Flash Memory Library

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

// --- Preferences Instance (Flash Memory) ---
Preferences prefs;

// Global runtime variables (Loaded from Flash on setup)
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
    loadStateFromFlash(); // Pull state safely from permanent flash storage
    
    Serial.printf("[Flash Cache] First Boot: %s | Has Valid Data: %s\n", rtcIsFirstBoot ? "TRUE" : "FALSE", rtcHasValidData ? "TRUE" : "FALSE");
    Serial.printf("[Flash Cache] Current Page Index: %d / Total Pages: %d\n", (int)rtcCurrentPageIndex, (int)rtcPageCount);
    Serial.printf("[Flash Cache] Tick Counter: %d | Fail Count: %d\n", rtcTickCounter, rtcFailCount);
    
    // 1. Initialize Display
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

    // 2. Network Sync Block
    if (!rtcHasValidData || rtcPageCount == 0) {
        Serial.println("[Network] Cache empty or expired. Connecting to Wi-Fi...");
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

        // Fetch fresh payload and write directly to Flash
        statusIcon = 'F';
        updateDisplay("Fetching API...");
        if (!fetchAPIData()) {
            Serial.println("[ERROR] API data fetch failed!");
            // handleError is called internally inside fetchAPIData
        }
        
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        
        rtcHasValidData = true;
        rtcCurrentPageIndex = 0; 
        rtcFailCount = 0;
        saveStateToFlash();
    }

    // 3. Page Rotation Cycle
    statusIcon = 'W'; 
    char stepBuffer[32];
    snprintf(stepBuffer, sizeof(stepBuffer), "Showing Page %d/%d", (int)rtcCurrentPageIndex + 1, (int)rtcPageCount);
    
    Serial.printf("[UI] Rendering %s...\n", stepBuffer);
    updateDisplay(stepBuffer);

    // Increment indices for next boot loop sequence
    rtcCurrentPageIndex++;
    rtcTickCounter += 10; 

    // If we have rendered the last page, clear the valid flag so next wake triggers an API poll
    if (rtcCurrentPageIndex >= rtcPageCount) {
        Serial.println("[Paging] Last page complete. Resetting cache variables for next cycle.");
        rtcCurrentPageIndex = 0; 
        rtcHasValidData = false; 
    }

    saveStateToFlash(); // Save variables before sleeping

    // 4. Deep Sleep
    statusIcon = 'S';
    Serial.printf("[DEEP SLEEP] Sleeping for %d seconds...\n\n", rtcPageDelaySec);
    esp_sleep_enable_timer_wakeup(rtcPageDelaySec * 1000000ULL);
    Serial.flush();
    esp_deep_sleep_start();
}

void loop() {}

// --- Flash Read/Write Helpers ---
void loadStateFromFlash() {
    prefs.begin("dash_state", true); // Open flash namespace in Read-Only mode
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
    prefs.begin("dash_state", false); // Open flash namespace in Read/Write mode
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
    
    // Top Bar Layout
    display.setFont(&FreeSans9pt7b);
    struct tm timeinfo;
    char timeStr[6] = "--:--";
    if (getLocalTime(&timeinfo) && timeinfo.tm_year >= 120) { 
        strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    }
    display.setCursor(5, 20);
    display.print(timeStr);

    char topRightStr[16];
    snprintf(topRightStr, sizeof(topRightStr), "N/A%% [%c]", statusIcon);
    display.setCursor(display.width() - 85, 20);
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

    display.setFont(&FreeSans9pt7b);
    display.setCursor(5, 50);
    display.printf("%s (T+%d)", stepMsg, rtcTickCounter);

    // Read page content dynamically out of Flash storage keys
    prefs.begin("dash_pages", true);
    char pageKey[16];
    snprintf(pageKey, sizeof(pageKey), "p_%d_cnt", (int)rtcCurrentPageIndex);
    int itemsInPage = prefs.getInt(pageKey, 0);

    int yStart = 80;
    for (int i = 0; i < itemsInPage; i++) {
        char kStr[32] = "";
        char vStr[64] = "";
        
        snprintf(pageKey, sizeof(pageKey), "p_%d_k_%d", (int)rtcCurrentPageIndex, i);
        prefs.getString(pageKey, kStr, sizeof(kStr));
        
        snprintf(pageKey, sizeof(pageKey), "p_%d_v_%d", (int)rtcCurrentPageIndex, i);
        prefs.getString(pageKey, vStr, sizeof(vStr));

        display.setCursor(5, yStart);
        display.printf("%s: %s", kStr, vStr);
        yStart += 30;
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
    Serial.println("[NTP] Contacting time servers for Oslo Sync...");
    configTime(0, 0, "no.pool.ntp.org", "pool.ntp.org");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    
    struct tm timeinfo;
    int retry = 0;
    while ((!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) && retry++ < 20) { delay(500); Serial.print("."); }
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

    // Clear old page keys out of Flash namespace to keep data clean
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

            char storeKey[24];
            snprintf(storeKey, sizeof(storeKey), "p_%d_k_%d", (int)rtcPageCount, itemCount);
            prefs.putString(storeKey, k);

            snprintf(storeKey, sizeof(storeKey), "p_%d_v_%d", (int)rtcPageCount, itemCount);
            prefs.putString(storeKey, v);

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
    
    updateDisplay("System Error");
    
    esp_sleep_enable_timer_wakeup(30 * 1000000ULL);
    Serial.flush();
    esp_deep_sleep_start();
}