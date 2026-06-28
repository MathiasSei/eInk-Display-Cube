#pragma once
#include <Arduino.h>

// Note: Network Credentials (LOCAL_SSID, LOCAL_PASS, AWS_API_KEY) 
// are injected globally via platformio.ini and secret_flags.ini

// --- Pin Definitions ---
#define EINK_SCL   4   
#define EINK_SDA   6   
#define EINK_CS    10  
#define EINK_DC    2   
#define EINK_RES   3   
#define EINK_BUSY  1   
#define BATTERY_PIN 0

// --- Global Shared Non-Volatile States ---
extern size_t rtcPageCount;
extern size_t rtcCurrentPageIndex;
extern uint32_t rtcPageDelaySec; 
extern uint32_t rtcTickCounter;
extern uint32_t rtcFailCount;
extern bool rtcHasValidData;
extern bool rtcIsFirstBoot;
extern bool rtcTriggerFullRefresh; 
extern char rtcErrorMessage[64]; 
extern char statusIcon; 

// --- Battery Moving Average Tracker ---
extern int rtcBatteryHistory[4];
extern int rtcBatteryReadingCount;

extern const char* awsEndpoint;

// --- Shared Core Helpers ---
void loadStateFromFlash();
void saveStateToFlash();
void handleError(const char* conceptualError);