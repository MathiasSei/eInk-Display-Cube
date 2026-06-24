#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

// E-ink Display Libraries
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold9pt7b.h>

// ==========================================
// AUTOMATED VERSIONING FALLBACK
// ==========================================
#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION "v0.0.0-local"
#endif

// Your GitHub repository target release link
const char* githubReleaseUrl = "https://github.com/YOUR_GITHUB_USERNAME/YOUR_REPO_NAME/releases/latest/download/firmware.bin";

// ==========================================
// YOUR EXACT ESP32-C3 TO E-INK PIN MAPPING
// ==========================================
#define EINK_SS    10  // CS
#define EINK_DC    2   // D/C
#define EINK_RST   3   // RES
#define EINK_BUSY  1   // BUSY

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(EINK_SS, EINK_DC, EINK_RST, EINK_BUSY));

// RTC Memory Counter to track deep sleep cycles visually
RTC_DATA_ATTR int wakeCount = 0; 

// ==========================================
// OVER-THE-AIR (OTA) UPDATE ROUTINE
// ==========================================
void checkForUpdates() {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFiClientSecure client;
    client.setInsecure(); 
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    Serial.printf("📡 Checking GitHub... Current version: %s\n", FIRMWARE_VERSION);
    
    t_httpUpdate_return ret = httpUpdate.update(client, githubReleaseUrl, FIRMWARE_VERSION);

    switch(ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("❌ OTA Failed (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("✅ Firmware is up-to-date.");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("🎉 Success! Restarting...");
            ESP.restart();
            break;
    }
}

// ==========================================
// MAIN EXECUTION FLOW
// ==========================================
void setup() {
    Serial.begin(115200);
    wakeCount++;
    
    Serial.println("\n--- ESP32-C3 Wake Cycle ---");
    
    // Initialize E-ink Panel using mapped pins
    SPI.begin(4, -1, 6, EINK_SS); 
    display.init(115200); 

    // Antenna Brownout Patch: Lower transmit power to stabilize weak connections
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_15dBm); 

    Serial.print("📶 Connecting to Wi-Fi...");
    // WIFI_SSID and WIFI_PASS are fed directly by your Environment Variables!
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attemptCounter = 0;
    while (WiFi.status() != WL_CONNECTED && attemptCounter < 20) {
        delay(500);
        Serial.print(".");
        attemptCounter++;
    }

    bool wifiSuccess = (WiFi.status() == WL_CONNECTED);
    String connectedNetwork = wifiSuccess ? WIFI_SSID : "CONNECTION FAILED";

    if (wifiSuccess) {
        Serial.println("\n✅ Connected!");
        checkForUpdates();
    } else {
        Serial.println("\n❌ Wi-Fi Connection failed.");
    }

    // Render Diagnostics UI to E-ink
    display.setRotation(1); 
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(GxEPD_BLACK);

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(10, 25);
        display.print("SYSTEM DIAGNOSTICS");
        display.drawFastHLine(0, 35, display.width(), GxEPD_BLACK);
        
        display.setCursor(10, 60);
        display.print("Version: ");
        display.print(FIRMWARE_VERSION);
        
        display.setCursor(10, 95);
        display.print("Net: ");
        display.print(connectedNetwork);
        
        display.setCursor(10, 130);
        display.print("Total Wakes: ");
        display.print(wakeCount);
    } while (display.nextPage());

    // Disconnect peripherals to safe standby current draws
    WiFi.disconnect(true);
    display.powerOff(); 

    Serial.println("😴 Entering deep sleep...");
    ESP.deepSleep(60 * 1000000); 
}

void loop() {
    // Unused
}