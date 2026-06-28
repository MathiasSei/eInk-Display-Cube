#include "network_time.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_wifi.h>

extern Preferences prefs;

bool connectWiFi() {
    WiFi.mode(WIFI_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B); 
    WiFi.begin(LOCAL_SSID, LOCAL_PASS);
    esp_wifi_set_ps(WIFI_PS_NONE); 
    WiFi.setTxPower(WIFI_POWER_15dBm); 

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry++ < 40) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    return (WiFi.status() == WL_CONNECTED);
}

void syncTime() {
    Serial.println("[NTP] Syncing Oslo Time...");
    configTzTime("CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", "no.pool.ntp.org", "pool.ntp.org");
    struct tm timeinfo;
    int retry = 0;
    while ((!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) && retry++ < 20) { 
        delay(500); 
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
        http.end(); 
        handleError("HTTP Conn Fail"); 
        return false;
    }

    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) { 
        http.end(); 
        handleError("JSON Parse Fail"); 
        return false; 
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