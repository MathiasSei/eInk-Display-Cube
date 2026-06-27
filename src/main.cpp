#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
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
RTC_DATA_ATTR bool rtcHasValidData = false;
RTC_DATA_ATTR bool rtcIsFirstBoot = true;

// Global runtime variables
char statusIcon = 'F'; 
const char* awsEndpoint = "https://lowbd437e3.execute-api.eu-north-1.amazonaws.com/v1/data";

// --- Forward Declarations ---
void updateDisplay(Page* page, const char* stepMsg);
void handleError();
void syncTime();
bool fetchAPIData();

void setup() {
    Serial.begin(115200);
    
    // 1. Manually route SPI pins to match your board
    SPI.begin(EINK_SCL, -1, EINK_SDA, EINK_CS); 
    display.init(115200, rtcIsFirstBoot, 10, false);
    display.setRotation(1); // 90° Clockwise rotation

    if (rtcIsFirstBoot) {
        rtcIsFirstBoot = false;
        rtcHasValidData = false;
        rtcCurrentPageIndex = 0;
        rtcTickCounter = 0;
        rtcFailCount = 0;
    }

    // 2. Connect to Wi-Fi
    if (rtcIsFirstBoot || !rtcHasValidData || rtcCurrentPageIndex == 0) {
        if (rtcFailCount > 0) statusIcon = 'R'; // Re-connecting indicator
        
        WiFi.begin(LOCAL_SSID, LOCAL_PASS);
        int retry = 0;
        while (WiFi.status() != WL_CONNECTED && retry++ < 30) {
            delay(500);
        }

        if (WiFi.status() != WL_CONNECTED) {
            handleError();
        }

        syncTime();

        // 3. Fetch Data if cache is stale/empty
        statusIcon = 'F';
        updateDisplay(nullptr, "Fetching API...");
        if (!fetchAPIData()) {
            handleError();
        }
        
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        rtcHasValidData = true;
        rtcFailCount = 0; 
    }

    // 4. Page Rotation Cycle
    statusIcon = 'W'; // Waiting state while page displays
    char stepBuffer[32];
    snprintf(stepBuffer, sizeof(stepBuffer), "Showing Page %d/%d", (int)rtcCurrentPageIndex + 1, (int)rtcPageCount);
    
    updateDisplay(&rtcPages[rtcCurrentPageIndex], stepBuffer);

    // Increment pagination indices
    rtcCurrentPageIndex++;
    rtcTickCounter += 10; // Simple increment tick logic

    if (rtcCurrentPageIndex >= rtcPageCount) {
        rtcCurrentPageIndex = 0; // Next wake will force a fresh API pull
        rtcHasValidData = false; 
    }

    // 5. Enter Deep Sleep
    statusIcon = 'S';
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
    char timeStr[6] = "00:00";
    if (getLocalTime(&timeinfo)) {
        strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    }
    display.setCursor(5, 20);
    display.print(timeStr);

    // Right-aligned status and battery placeholder
    char topRightStr[16];
    snprintf(topRightStr, sizeof(topRightStr), "N/A%% [%c]", statusIcon);
    display.setCursor(display.width() - 80, 20);
    display.print(topRightStr);

    // Horizontal line separator
    display.drawFastHLine(0, 30, display.width(), GxEPD_BLACK);

    // --- Error Mode Treatment ---
    if (statusIcon == 'X') {
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(85, 120);
        display.print(":(");
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
    // If it's the very first render, do a full flash to clear ghosting. Otherwise, partial.
    if (rtcIsFirstBoot && rtcCurrentPageIndex == 0) {
        display.firstPage();
        do {
            drawLayout(page, stepMsg);
        } while (display.nextPage());
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
        display.firstPage();
        do {
            drawLayout(page, stepMsg);
        } while (display.nextPage());
    }
}

// --- Data & Sync Functions ---
void syncTime() {
    // Set time zone directly to Oslo/Europe
    configT9542(0, 0, "no.pool.ntp.org", "pool.ntp.org");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry++ < 10) {
        delay(500);
    }
}

bool fetchAPIData() {
    HTTPClient http;
    http.begin(awsEndpoint);
    http.addHeader("X-API-Key", AWS_API_KEY);
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    // Process Payload safely
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream());
    
    if (error) {
        http.end();
        return false;
    }

    // Override system delay configuration if passed
    if (doc.containsKey("pageDelaySec")) {
        rtcPageDelaySec = doc["pageDelaySec"].as<uint32_t>();
    }

    // Map parsing arrays safely into persistent structure
    JsonArray pagesArray = doc["pages"].as<JsonArray>();
    rtcPageCount = 0;

    for (JsonObject p : pagesArray) {
        if (rtcPageCount >= 10) break; // Hard ceiling rule compliance

        Page customPage;
        JsonArray itemsArray = p["items"].as<JsonArray>();
        customPage.itemCount = 0;

        for (JsonObject item : itemsArray) {
            if (customPage.itemCount >= 3) break; // Max 3 items per page

            size_t idx = customPage.itemCount;
            strlcpy(customPage.items[idx].key, item["key"] | "", sizeof(customPage.items[idx].key));
            strlcpy(customPage.items[idx].value, item["value"] | "", sizeof(customPage.items[idx].value));
            customPage.itemCount++;
        }
        
        rtcPages[rtcPageCount] = customPage;
        rtcPageCount++;
    }

    http.end();
    statusIcon = 'R'; 
    return (rtcPageCount > 0);
}

void handleError() {
    statusIcon = 'X';
    rtcFailCount++;
    rtcHasValidData = false;
    
    updateDisplay(nullptr, "System Error");
    
    // Sleep for 30 seconds before executing an explicit hardware cycle retry
    esp_sleep_enable_timer_wakeup(30 * 1000000ULL);
    esp_deep_sleep_start();
}

// --- Disabled OTA updates wrapper placeholder ---
/*
void checkOTAUpdates() {
    // Disabled intentionally for now.
}
*/