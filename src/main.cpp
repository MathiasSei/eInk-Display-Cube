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
const char* githubReleaseUrl = "https://github.com/MathiasSei/Projects/releases/latest/download/firmware.bin";
const char* githubVersionUrl = "https://github.com/MathiasSei/Projects/releases/latest/download/version.txt";

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

int tickCount = 0;
bool isFirstDraw = true;

// Helper function to turn numeric Wi-Fi status codes into readable text strings
const char* getWiFiStatusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS:     return "WL_IDLE_STATUS (Station shifting states)";
        case WL_NO_SSID_AVAIL:   return "WL_NO_SSID_AVAIL (Network name cannot be found)";
        case WL_SCAN_COMPLETED:  return "WL_SCAN_COMPLETED";
        case WL_CONNECTED:       return "WL_CONNECTED (IP Address secured)";
        case WL_CONNECT_FAILED:  return "WL_CONNECT_FAILED (Bad credentials/Auth rejection)";
        case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED:    return "WL_DISCONNECTED (Radio offline)";
        default:                 return "UNKNOWN_ERROR_CODE";
    }
}

// ==========================================
// E-INK DIAGNOSTICS DISPLAY ROUTINE
// ==========================================
void updateDisplay(const char* step, int tick) {
    Serial.printf("\n📺 [Display] Updating: Step='%s', Tick=%d\n", step, tick);
    display.setRotation(3); // Turn screen 90 degrees clockwise
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    
    // For the first draw, do a full refresh to clean any old image.
    // For subsequent draws, use partial refresh.
    if (!isFirstDraw) {
        display.setPartialWindow(0, 0, display.width(), display.height());
    } else {
        isFirstDraw = false;
    }
    
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(10, 20);
        display.print("DEBUG DIAGNOSTIC");
        display.drawFastHLine(0, 28, display.width(), GxEPD_BLACK);
        
        display.setCursor(10, 56);
        display.print("Version: ");
        display.print(FIRMWARE_VERSION);
        
        display.setCursor(10, 84);
        display.print("SSID: ");
        display.print(WIFI_SSID);
        
        display.setCursor(10, 112);
        display.print("PASS: ");
        display.print(WIFI_PASS);
        
        display.setCursor(10, 140);
        display.print("Step: ");
        display.print(step);
        
        display.setCursor(10, 168);
        display.print("Tick: ");
        display.print(tick);
    } while (display.nextPage());
    Serial.println("✅ [Display] Update finished.");
}

// ==========================================
// OVER-THE-AIR (OTA) UPDATE ROUTINE
// ==========================================
void checkForUpdates() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ OTA Cancelled: Cannot check for updates because Wi-Fi is not connected.");
        updateDisplay("OTA Cancel: No WiFi", tickCount);
        return;
    }

    Serial.println("\n[OTA] Checking for available updates on GitHub...");
    updateDisplay("Checking OTA", tickCount);
    WiFiClientSecure client;
    client.setInsecure(); 

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    Serial.printf("[OTA] Local Version: %s\n", FIRMWARE_VERSION);
    Serial.printf("[OTA] Querying version tag from: %s\n", githubVersionUrl);
    
    if (http.begin(client, githubVersionUrl)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String latestVersion = http.getString();
            latestVersion.trim(); // Clean up whitespace/newlines
            Serial.printf("[OTA] Latest Version: %s\n", latestVersion.c_str());
            
            if (latestVersion != FIRMWARE_VERSION) {
                Serial.printf("[OTA] New firmware version %s available. Starting download...\n", latestVersion.c_str());
                updateDisplay("Downloading OTA...", tickCount);
                
                httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
                t_httpUpdate_return ret = httpUpdate.update(client, githubReleaseUrl);
                
                switch(ret) {
                    case HTTP_UPDATE_FAILED:
                        Serial.printf("❌ [OTA] Update failed! Error (%d): %s\n", 
                                      httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
                        updateDisplay("OTA Failed", tickCount);
                        break;
                    case HTTP_UPDATE_NO_UPDATES:
                        Serial.println("✅ [OTA] No updates found.");
                        updateDisplay("OTA No Updates", tickCount);
                        break;
                    case HTTP_UPDATE_OK:
                        Serial.println("🎉 [OTA] Success! New firmware flashed. Restarting MCU...");
                        updateDisplay("OTA Success: Restart", tickCount);
                        delay(500);
                        ESP.restart();
                        break;
                }
            } else {
                Serial.println("✅ [OTA] Device is up to date. Skipping update.");
                updateDisplay("OTA Up-to-date", tickCount);
            }
        } else {
            Serial.printf("❌ [OTA] Version check failed. HTTP Error: %d\n", httpCode);
            updateDisplay("OTA Check Error", tickCount);
        }
        http.end();
    } else {
        Serial.println("❌ [OTA] Connection to GitHub version check failed.");
        updateDisplay("OTA Connect Error", tickCount);
    }
}

// ==========================================
// MAIN EXECUTION FLOW
// ==========================================
void setup() {
    // 1. Fire up Serial immediately with a delay to let your laptop latch onto the serial bus
    Serial.begin(115200);
    delay(2500); 
    // 👇 ADD THIS LINE: Wait up to 3 seconds for the USB virtual serial line to initialize
    while (!Serial && millis() < 3000) { 
        delay(10); 
    }
    
    wakeCount++;
    Serial.println("\n==================================================");
    Serial.println("🚀 ESP32-C3 HARDWARE BOOTED SUCCESSFULLY");
    Serial.printf("🔋 Current Deep Sleep Wake Counter: %d\n", wakeCount);
    Serial.printf("🏷️ Code Build Identification String: %s\n", FIRMWARE_VERSION);
    Serial.println("==================================================");

    // 2. Initialize E-ink Panel with diagnostic prints
    Serial.println("\n🎬 [Hardware-Init] Starting SPI Bus mapping adjustments...");
    Serial.printf("   -> MOSI/SDA Pin: 6 | SCK/SCL Pin: 4 | CS Pin: %d\n", EINK_SS);
    SPI.begin(4, -1, 6, EINK_SS); 
    
    Serial.println("🎬 [Hardware-Init] Initializing GxEPD2 screen controller panel...");
    display.init(115200); 
    Serial.println("✅ [Hardware-Init] E-ink Panel driver engine mounted.");

    updateDisplay("Booting...", tickCount);

    // 3. Radio optimization adjustments
    Serial.println("\n🔧 [Radio-Config] Forcing Station Mode configuration settings...");
    WiFi.mode(WIFI_STA);
    
    Serial.println("🔧 [Radio-Config] Applying internal RF power attenuation patch...");
    Serial.println("   -> Setting maximum TX output down to 15dBm to bypass impedance brownouts.");
    WiFi.setTxPower(WIFI_POWER_15dBm); 

    // 4. Trace credentials injection securely (prints length and string characters)
    Serial.println("\n📶 [Wi-Fi] Loading target connection parameters...");
    Serial.printf("   -> Target Access Point Name (SSID): [%s] (Length: %d characters)\n", WIFI_SSID, strlen(WIFI_SSID));
    Serial.printf("   -> Target Private Passphrase (PASS): [%s] (Length: %d characters)\n", WIFI_PASS, strlen(WIFI_PASS));

    if(strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "YOUR_LOCAL_WIFI") == 0) {
        Serial.println("⚠️ WARNING: Your environment variables may have compiled as blank dummy templates!");
    }

    updateDisplay("WiFi Connecting", tickCount);

    Serial.print("📶 [Wi-Fi] Sending radio connection request sequence...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attemptCounter = 0;
    wl_status_t trackingStatus = WiFi.status();
    
    while (trackingStatus != WL_CONNECTED && attemptCounter < 30) {
        delay(1000);
        attemptCounter++;
        trackingStatus = WiFi.status();
        
        Serial.printf("\n   [Attempt %02d/30] Hardware Status Code: %d -> %s", 
                      attemptCounter, trackingStatus, getWiFiStatusName(trackingStatus));
    }

    bool wifiSuccess = (trackingStatus == WL_CONNECTED);

    if (wifiSuccess) {
        Serial.println("\n\n✅ [Wi-Fi] Connection authenticated successfully!");
        Serial.print("   -> Internal Local Static IP Target Address: ");
        Serial.println(WiFi.localIP());
        
        updateDisplay("WiFi Connected", tickCount);
        
        // Advance directly to update tracking block
        checkForUpdates();
    } else {
        Serial.println("\n\n❌ [Wi-Fi] Connection timed out after 30 seconds.");
        Serial.printf("   -> Termination Status: %s\n", getWiFiStatusName(trackingStatus));
        Serial.println("   -> Suggestion: Check terminal environments or solder a bulk cap across 3V3/GND.");
        updateDisplay("WiFi Fail", tickCount);
    }

    Serial.println("\nℹ️ [System] Entering continuous development loop mode...");
    updateDisplay("Ready / Running", tickCount);
}

unsigned long lastTickTime = 0;

void loop() {
    if (millis() - lastTickTime >= 10000) {
        lastTickTime = millis();
        tickCount++;
        Serial.printf("[Loop] Tick: %d\n", tickCount);
        
        String stepMsg = "Running (WiFi: ";
        if (WiFi.status() == WL_CONNECTED) {
            stepMsg += "ON)";
        } else {
            stepMsg += "OFF)";
        }
        
        updateDisplay(stepMsg.c_str(), tickCount);
    }
    delay(100);
}