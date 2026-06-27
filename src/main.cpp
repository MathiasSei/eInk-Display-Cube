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

// --- Pin Definitions ---
#define EINK_SCL   4   // Clock (SCK)
#define EINK_SDA   6   // Data (MOSI)
#define EINK_CS    10  // Chip Select
#define EINK_DC    2   // Data/Command
#define EINK_RES   3   // Reset
#define EINK_BUSY  1   // Busy

// --- Display Selector (WeAct 1.54" 200x200 uses SSD1681) ---
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(EINK_CS, EINK_DC, EINK_RES, EINK_BUSY)
);

// --- Data Structures for RTC Memory ---
struct KeyValue {
    char key[32];
    char value[64];
};

struct Page {
    KeyValue items[3];
    size_t itemCount = 0;
};

// Persistent variables tracking across deep sleep restarts
RTC_DATA_ATTR Page rtcPages[10];
RTC_DATA_ATTR size_t rtcPageCount = 0;
RTC_DATA_ATTR size_t rtcCurrentPageIndex = 0;
RTC_DATA_ATTR uint32_t rtcPageDelaySec = 45; 
RTC_DATA_ATTR uint32_t rtcTickCounter = 0;
RTC_DATA_ATTR uint32_t rtcFailCount = 0;
RTC_DATA_ATTR bool rtcHasValidData = false; // Remains true until the final page cycles out
RTC_DATA_ATTR bool rtcIsFirstBoot = true;
RTC_DATA_ATTR char rtcErrorMessage[64] = "No Error"; 

// Global runtime variables
char statusIcon = 'F'; 
const char* awsEndpoint = "https://lowbd437e3.execute-api.eu-north-1.amazonaws.com/v1/data";

// --- Forward Declarations ---
void updateDisplay(Page* page, const char* stepMsg);
void handleError(const char* conceptualError);
void syncTime();
bool fetchAPIData();

void setup() {
    Serial.begin(115200);
    delay(1000); 
    
    Serial.println("\n--- ESP32-C3 Wakeup / Power On ---");
    Serial.printf("First Boot Flag: %s\n", rtcIsFirstBoot ? "TRUE" : "FALSE");
    Serial.printf("Has Cached Valid Data: %s\n", rtcHasValidData ? "TRUE" : "FALSE");
    Serial.printf("Current Page Index: %d\n", (int)rtcCurrentPageIndex);
    Serial.printf("Total Pages Available: %d\n", (int)rtcPageCount);
    Serial.printf("Accumulated Tick Counter: %d\n", rtcTickCounter);
    
    // 1. Manually route SPI pins to match your board
    Serial.println("[Display] Initializing custom SPI & E-Ink screen...");
    SPI.begin(EINK_SCL, -1, EINK_SDA, EINK_CS); 
    display.init(115200, rtcIsFirstBoot, 10, false);
    display.setRotation(3); // 270° rotation

    if (rtcIsFirstBoot) {
        rtcIsFirstBoot = false;
        rtcHasValidData = false;
        rtcCurrentPageIndex = 0;
        rtcTickCounter = 0;
        rtcFailCount = 0;
        strlcpy(rtcErrorMessage, "None", sizeof(rtcErrorMessage));
    }

    // 2. Network Check: Connect ONLY if cache is flat out empty or explicitly invalidated
    if (!rtcHasValidData) {
        Serial.println("[Network] Active cache empty or expired. Initiating network sync...");
        if (rtcFailCount > 0) statusIcon = 'R'; 
        
        Serial.printf("[WiFi] Connecting to SSID: %s\n", LOCAL_SSID);
        WiFi.mode(WIFI_STA);
        WiFi.begin(LOCAL_SSID, LOCAL_PASS);

        // Clamped specifically to 15 dBm as requested
        WiFi.setTxPower(WIFI_POWER_15dBm); 
        Serial.printf("[WiFi] Radio TX Power manually restricted to: %d dBm\n", WiFi.getTxPower());

        int retry = 0;
        while (WiFi.status() != WL_CONNECTED && retry++ < 30) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[ERROR] Wi-Fi Connection failed!");
            handleError("WiFi Disconnected");
        }
        Serial.printf("[WiFi] Connected! IP Address: %s\n", WiFi.localIP().toString().c_str());

        syncTime();

        // 3. Fetch fresh payload and populate structure
        statusIcon = 'F';
        Serial.println("[API] Requesting fresh dashboard state from AWS...");
        updateDisplay(nullptr, "Fetching API...");
        
        if (!fetchAPIData()) {
            Serial.println("[ERROR] Fetching API or Parsing JSON payload failed!");
            // handleError is called inside fetchAPIData with specific logs
        }
        
        Serial.println("[Network] Turning off Wi-Fi radio to conserve power...");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        
        rtcHasValidData = true; // Mark cache valid for the entirety of this loop run
        rtcCurrentPageIndex = 0; // Reset page layout target to start fresh
        rtcFailCount = 0; 
    } else {
        Serial.println("[Cache] Valid pages found in RTC memory. Skipping network fetch.");
    }

    // 4. Page Rotation Cycle
    statusIcon = 'W'; 
    char stepBuffer[32];
    snprintf(stepBuffer, sizeof(stepBuffer), "Showing Page %d/%d", (int)rtcCurrentPageIndex + 1, (int)rtcPageCount);
    
    Serial.printf("[UI] Rendering %s from local cache storage...\n", stepBuffer);
    updateDisplay(&rtcPages[rtcCurrentPageIndex], stepBuffer);

    // Increment indices for next boot loop sequence
    rtcCurrentPageIndex++;
    rtcTickCounter += 10; 

    // Critical change: Only invalidate the data cache once we finish rendering the final page
    if (rtcCurrentPageIndex >= rtcPageCount) {
        Serial.println("[Paging] Final page reached. Next cycle will trigger fresh API updates.");
        rtcCurrentPageIndex = 0; 
        rtcHasValidData = false; // Invalidate cache so next wake forces an API fetch
    }

    // 5. Enter Deep Sleep
    statusIcon = 'S';
    Serial.println("===============================================");
    Serial.printf("[DEEP SLEEP] Entering Ultra Low Power Mode Now.\n");
    Serial.printf("[DEEP SLEEP] Setup Timer Duration: %d seconds.\n", rtcPageDelaySec);
    Serial.println("===============================================\n");
    
    esp_sleep_enable_timer_wakeup(rtcPageDelaySec * 1000000ULL);
    Serial.flush();
    esp_deep_sleep_start();
}

void loop() {
    // Left empty because of Deep Sleep
}

// --- Display Logic ---
void drawLayout(Page* page, const char* stepMsg) {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    
    // --- Top Bar ---
    display.setFont(&FreeSans9pt7b);
    
    // Clock (Oslo Time)
    struct tm timeinfo;
    char timeStr[6] = "--:--";
    if (getLocalTime(&timeinfo) && timeinfo.tm_year >= 120) { 
        strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    }
    display.setCursor(5, 20);
    display.print(timeStr);

    // Right-aligned status and battery placeholder
    char topRightStr[16];
    snprintf(topRightStr, sizeof(topRightStr), "N/A%% [%c]", statusIcon);
    display.setCursor(display.width() - 85, 20);
    display.print(topRightStr);

    // Horizontal line separator
    display.drawFastHLine(0, 30, display.width(), GxEPD_BLACK);

    // --- Error Mode Treatment ---
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

    // --- Middle Line (Step & Tick Counter) ---
    display.setFont(&FreeSans9pt7b);
    display.setCursor(5, 50);
    display.printf("%s (T+%d)", stepMsg, rtcTickCounter);

    // --- Page Content Rendering ---
    if (page != nullptr) {
        int yStart = 80;
        for (size_t i = 0; i < page->itemCount; i++) {
            display.setFont(&FreeSans9pt7b);
            display.setCursor(5, yStart);
            display.printf("%s: %s", page->items[i].key, page->items[i].value);
            yStart += 30;
        }
    }
}

void updateDisplay(Page* page, const char* stepMsg) {
    // Treat the true initial boot as a full fresh block, subsequent updates stay partial
    if (rtcTickCounter == 0 && rtcCurrentPageIndex == 0) {
        Serial.println("[Display] Executing heavy FULL refresh cycle...");
        display.firstPage();
        do {
            drawLayout(page, stepMsg);
        } while (display.nextPage());
    } else {
        Serial.println("[Display] Executing quick PARTIAL window refresh...");
        display.setPartialWindow(0, 0, display.width(), display.height());
        display.firstPage();
        do {
            drawLayout(page, stepMsg);
        } while (display.nextPage());
    }
    Serial.println("[Display] Refresh cycle drawing finished.");
}

// --- Data & Sync Functions ---
void syncTime() {
    Serial.println("[NTP] Contacting time servers for Oslo Sync...");
    configTime(0, 0, "no.pool.ntp.org", "pool.ntp.org");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    
    struct tm timeinfo;
    int retry = 0;
    while ((!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) && retry++ < 20) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    
    if (getLocalTime(&timeinfo) && timeinfo.tm_year >= 120) {
        char debugTime[32];
        strftime(debugTime, sizeof(debugTime), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Serial.printf("[NTP] Oslo Time verified successfully: %s\n", debugTime);
    } else {
        Serial.println("[NTP] Warning: Could not resolve current NTP epoch string.");
    }
}

bool fetchAPIData() {
    WiFiClientSecure client;
    client.setInsecure(); 

    HTTPClient http;
    Serial.printf("[HTTP] Connecting to endpoint safely: %s\n", awsEndpoint);
    
    http.begin(client, awsEndpoint);
    http.addHeader("X-API-Key", AWS_API_KEY);
    
    int httpCode = http.GET();
    Serial.printf("[HTTP] Server responded with code: %d\n", httpCode);
    
    if (httpCode != HTTP_CODE_OK) {
        char errBuf[48];
        snprintf(errBuf, sizeof(errBuf), "HTTP Error: %d", httpCode);
        http.end();
        handleError(errBuf);
        return false;
    }

    String payload = http.getString();
    Serial.println("-----------------------------------------------\n[DEBUG RAW JSON DATA FROM SERVER]:");
    Serial.println(payload);
    Serial.println("-----------------------------------------------");

    Serial.println("[JSON] Parsing payload into local memory layout...");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        char errBuf[48];
        snprintf(errBuf, sizeof(errBuf), "JSON Format Err: %s", error.c_str());
        http.end();
        handleError(errBuf);
        return false;
    }

    if (doc["pageDelaySec"]) {
        rtcPageDelaySec = doc["pageDelaySec"].as<uint32_t>();
        Serial.printf("[JSON] Global page delay parameter updated: %d seconds\n", rtcPageDelaySec);
    }

    JsonArray pagesArray = doc["pages"].as<JsonArray>();
    rtcPageCount = 0;

    for (JsonObject p : pagesArray) {
        if (rtcPageCount >= 10) break; 

        Page customPage;
        JsonArray itemsArray = p["items"].as<JsonArray>();
        customPage.itemCount = 0;

        Serial.printf("  Parsing Page [%d]:\n", (int)rtcPageCount + 1);
        for (JsonObject item : itemsArray) {
            if (customPage.itemCount >= 3) break; 

            size_t idx = customPage.itemCount;
            strlcpy(customPage.items[idx].key, item["key"] | "", sizeof(customPage.items[idx].key));
            strlcpy(customPage.items[idx].value, item["value"] | "", sizeof(customPage.items[idx].value));
            
            Serial.printf("    -> %s: %s\n", customPage.items[idx].key, customPage.items[idx].value);
            customPage.itemCount++;
        }
        
        rtcPages[rtcPageCount] = customPage;
        rtcPageCount++;
    }

    http.end();
    statusIcon = 'R'; 
    Serial.printf("[API] Parsing finished. Successfully imported %d dashboard pages.\n", rtcPageCount);
    return (rtcPageCount > 0);
}

void handleError(const char* conceptualError) {
    statusIcon = 'X';
    rtcFailCount++;
    rtcHasValidData = false;
    
    strlcpy(rtcErrorMessage, conceptualError, sizeof(rtcErrorMessage));
    
    Serial.printf("[FAIL STATE] Triggering alternative safety fallback sequence (Failures: %d, Reason: %s)\n", rtcFailCount, rtcErrorMessage);
    updateDisplay(nullptr, "System Error");
    
    Serial.println("===============================================");
    Serial.println("[DEEP SLEEP ERROR RETRY] Dropping to 30 second emergency cycle...");
    Serial.println("===============================================\n");
    
    esp_sleep_enable_timer_wakeup(30 * 1000000ULL);
    Serial.flush();
    esp_deep_sleep_start();
}
