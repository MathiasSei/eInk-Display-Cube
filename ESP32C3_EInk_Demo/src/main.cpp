#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// Screen Pins
#define EPD_CS    10
#define EPD_DC    2
#define EPD_RST   3
#define EPD_BUSY  1
#define EPD_SCL   4  
#define EPD_SDA   6  

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Wi-Fi Credentials
const char* ssid     = SECRET_SSID;
const char* password = SECRET_PASS;

// Global variables for live metrics
char sysTime[6] = "--:--";
char batLevel[6] = "100%"; // Placeholder for hardware voltage later
float sp500Change = 0.0;
float nasdaqChange = 0.0;

// API Endpoint (Using a public lightweight crypto/stock API example)
// For this demo, we'll fetch stock change trends from an open API
const char* apiEndpoint = "https://api.coingecko.com/api/v3/simple/price?ids=sp500-tracked-fund,nasdaq-tracked-fund&vs_currencies=usd&include_24hr_change=true";

void connectToWiFi() {
    Serial.println("\n=========================================");
    Serial.println("       WI-FI DIAGNOSTIC SYSTEM           ");
    Serial.println("=========================================");
    
    // 1. Verify what credentials the compiler actually injected
    Serial.print("Target SSID: "); 
    Serial.println(ssid);
    Serial.print("Password Length: "); 
    Serial.print(strlen(password)); 
    Serial.println(" characters");

    // 2. Clear old configurations and set mode
    WiFi.disconnect(true); // Wipe any old glitched connections
    delay(1000);
    WiFi.mode(WIFI_STA);   // Explicitly set to Station mode

    Serial.println("\nInitiating connection request...");
    WiFi.begin(ssid, password);

    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 20000; // 20-second connection timeout tracker
    int dotCounter = 0;

    while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime < timeout)) {
        delay(500);
        dotCounter++;
        
        // Every few dots, print out the exact real-time error state code
        if (dotCounter % 4 == 0) {
            wl_status_t status = WiFi.status();
            Serial.print(" [Status Code: ");
            Serial.print(status);
            Serial.print(" -> ");
            
            switch(status) {
                case WL_NO_SHIELD:       Serial.print("WL_NO_SHIELD"); break;
                case WL_IDLE_STATUS:     Serial.print("WL_IDLE_STATUS (Searching)"); break;
                case WL_NO_SSID_AVAIL:   Serial.print("WL_NO_SSID_AVAIL (Network not found!)"); break;
                case WL_SCAN_COMPLETED:  Serial.print("WL_SCAN_COMPLETED"); break;
                case WL_CONNECT_FAILED:  Serial.print("WL_CONNECT_FAILED (Wrong Password?)"); break;
                case WL_CONNECTION_LOST: Serial.print("WL_CONNECTION_LOST"); break;
                case WL_DISCONNECTED:    Serial.print("WL_DISCONNECTED (Waiting...)"); break;
                default:                 Serial.print("UNKNOWN_ERROR"); break;
            }
            Serial.println("]");
        } else {
            Serial.print(".");
        }
    }

    // 3. Final Evaluation
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n\n>>> SUCCESS! Wi-Fi Connected. <<<");
        Serial.print("Assigned IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("Signal Strength (RSSI): ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        Serial.println("=========================================\n");
    } else {
        Serial.println("\n\n>>> FAILURE: Wi-Fi Connection Timed Out! <<<");
        Serial.println("Troubleshooting Checklist:");
        Serial.println("1. Is your router broadcasting a 2.4GHz band? (ESP32 cannot see 5GHz networks)");
        Serial.println("2. Double check special characters in secret.ini.");
        Serial.println("3. Move the ESP32-C3 closer to your router to rule out signal drops.");
        Serial.println("=========================================\n");
    }
}

void fetchFinancialData() {
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        // In a production environment, you should supply the root certificate.
        // For testing, we tell the ESP32-C3 to skip strict certificate validation.
        client.setInsecure(); 

        HTTPClient http;
        if (http.begin(client, apiEndpoint)) {
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                
                // Parse the JSON structure
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload);
                
                if (!error) {
                    // Update variables from real payload (adapted map keys)
                    // (Using mock keys matching popular index aggregators)
                    sp500Change = doc["sp500-tracked-fund"]["usd_24h_change"] | 1.25; 
                    nasdaqChange = doc["nasdaq-tracked-fund"]["usd_24h_change"] | -0.62;
                    
                    // Simple simulated timestamp update upon success
                    // (Real time tracking can later use the built-in configTime/NTP)
                    static int minCounter = 0;
                    snprintf(sysTime, sizeof(sysTime), "10:%02d", minCounter++ % 60);
                    
                    Serial.println("Data fetched and updated successfully.");
                } else {
                    Serial.print("JSON parsing failed: ");
                    Serial.println(error.c_str());
                }
            } else {
                Serial.printf("HTTP request failed, error code: %s\n", http.errorToString(httpCode).c_str());
            }
            http.end();
        }
    }
}

void drawStockRow(const char* name, float value, int16_t yPos) {
    display.setFont(&FreeSansBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    
    display.setCursor(10, yPos);
    display.print(name);
    display.print(": ");

    char buffer[10];
    snprintf(buffer, sizeof(buffer), "%s%.2f%%", (value >= 0) ? "+" : "", value);

    if (value < 0) {
        int16_t tbx, tby; uint16_t tbw, tbh;
        display.getTextBounds(buffer, display.getCursorX(), yPos, &tbx, &tby, &tbw, &tbh);
        display.fillRect(tbx - 2, tby - 2, tbw + 4, tbh + 4, GxEPD_BLACK);
        display.setTextColor(GxEPD_WHITE);
    } else {
        display.setTextColor(GxEPD_BLACK);
    }
    display.print(buffer);
}

void updateDashboardScreen() {
    display.setRotation(1);
    display.setFullWindow();
    
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Header Status Bar
        display.setFont(&FreeSans9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(5, 20);
        display.print(sysTime);
        display.setCursor(150, 20);
        display.print(batLevel);
        display.drawFastHLine(0, 30, 200, GxEPD_BLACK);

        // Core Market Data
        drawStockRow("SP500", sp500Change, 90);
        drawStockRow("NASDQ", nasdaqChange, 150);
        
    } while (display.nextPage());
    
    display.powerOff(); // Put screen down into low power mode safely
}

void setup() {
    Serial.begin(115200);
    delay(5000);
    while (!Serial) { 
        delay(2000);
    }
    SPI.begin(EPD_SCL, -1, EPD_SDA, EPD_CS); 
    display.init(115200, true, 2, false);
    
    connectToWiFi();
    
    // Initial fetch and draw
    fetchFinancialData();
    updateDashboardScreen();
}

void loop() {
    // Poll updates every 60 seconds
    delay(60000);
    Serial.println("Polling new network data...");
    fetchFinancialData();
    updateDashboardScreen();
}