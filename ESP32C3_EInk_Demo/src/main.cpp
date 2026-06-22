#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// Screen Pins Configuration
#define EPD_CS    10
#define EPD_DC    2
#define EPD_RST   3
#define EPD_BUSY  1
#define EPD_SCL   4  
#define EPD_SDA   6  

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Environment Secret Declarations
const char* ssid     = SECRET_SSID;
const char* password = SECRET_PASS;

// Global application variables
char sysTime[6] = "--:--";
char batLevel[6] = "100%"; 
float sp500Change = 0.0;
float nasdaqChange = 0.0;

// Time Server Configuration (Set to your local UTC offset, e.g., 3600 for GMT+1)
const char* ntpServer  = "pool.ntp.org";
const long  gmtOffset_sec = 3600; 
const int   daylightOffset_sec = 3600;

void connectAndSync() {
    Serial.println("\n--- Initiating Wi-Fi Secure Connection ---");
    
    WiFi.disconnect(true);
    delay(500);
    WiFi.mode(WIFI_STA);
    WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);
    
    // PERMANENT ANTENNA FIX: Drop transmission power to mitigate RF reflection crash
    WiFi.setTxPower(WIFI_POWER_11dBm); 
    
    WiFi.begin(ssid, password);
    
    unsigned long startTry = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTry < 20000)) {
        delay(500);
        Serial.print(".");
    }
    
    if(WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected! IP: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        
        // Sync Time via NTP
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        struct tm timeinfo;
        if(getLocalTime(&timeinfo)){
            strftime(sysTime, sizeof(sysTime), "%H:%M", &timeinfo);
            Serial.printf("Time Synced: %s\n", sysTime);
        }
    } else {
        Serial.println("\nConnection timed out. Retrying next cycle.");
    }
}

// Update your endpoint string at the top of the file to use these verified asset IDs:
const char* apiEndpoint = "https://api.coingecko.com/api/v3/simple/price?ids=spdr-s-p-500-etf-trust,invesco-qqq-trust&vs_currencies=usd&include_24hr_change=true";

void fetchFinancialData() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ Cannot fetch data: Wi-Fi is disconnected!");
        return;
    }

    Serial.println("\n-----------------------------------------");
    Serial.println("        API HTTP REQUEST DEBUG           ");
    Serial.println("-----------------------------------------");
    Serial.printf("Pinging URL: %s\n", apiEndpoint);

    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate tracking to save RAM

    HTTPClient http;
    if (http.begin(client, apiEndpoint)) {
        int httpCode = http.GET();
        Serial.printf("HTTP Response Code from Server: %d\n", httpCode);

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            
            // CRITICAL DEBUG: Print the raw server payload directly to console
            Serial.println("--- RAW JSON PAYLOAD RECEIVED ---");
            Serial.println(payload); 
            Serial.println("---------------------------------");

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                // Extract using the updated valid tracking keys
                sp500Change  = doc["spdr-s-p-500-etf-trust"]["usd_24h_change"] | 0.0; 
                nasdaqChange = doc["invesco-qqq-trust"]["usd_24h_change"] | 0.0;
                
                Serial.println("Parsed values successfully:");
                Serial.printf(" -> S&P 500 (SPY Proxy): %.2f%%\n", sp500Change);
                Serial.printf(" -> Nasdaq (QQQ Proxy): %.2f%%\n", nasdaqChange);
            } else {
                Serial.print("❌ JSON Deserialization Failed: ");
                Serial.println(error.c_str());
            }
        } else {
            Serial.printf("❌ HTTP GET Request Failed. Error string: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    } else {
        Serial.println("❌ Unable to connect to the API server host.");
    }
    
    // Refresh local time tracking
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)){
        strftime(sysTime, sizeof(sysTime), "%H:%M", &timeinfo);
    }
    Serial.println("=========================================\n");
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
    display.setRotation(1); // 90-degree layout consistency rule
    display.setFullWindow();
    
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Header Structure
        display.setFont(&FreeSans9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(5, 20);
        display.print(sysTime);
        display.setCursor(150, 20);
        display.print(batLevel);
        display.drawFastHLine(0, 30, 200, GxEPD_BLACK);

        // Indices Blocks
        drawStockRow("SP500", sp500Change, 90);
        drawStockRow("NASDQ", nasdaqChange, 150);
        
    } while (display.nextPage());
    
    display.powerOff(); 
}

void setup() {
    Serial.begin(115200);
    
    // Wait max 5 seconds for serial connection setup
    unsigned long entry = millis();
    while (!Serial && (millis() - entry < 5000)) { delay(10); }

    SPI.begin(EPD_SCL, -1, EPD_SDA, EPD_CS); 
    display.init(115200, true, 2, false);
    
    // Run initial pipeline loop
    connectAndSync();
    fetchFinancialData();
    updateDashboardScreen();
}

void loop() {
    // Poll updates every 15 seconds
    delay(15000);
    
    // Verify network sanity before execution pass
    if (WiFi.status() != WL_CONNECTED) {
        connectAndSync();
    }
    
    fetchFinancialData();
    updateDashboardScreen();
}