#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h> // Bold font for financial indices

#define EPD_CS    10
#define EPD_DC    2
#define EPD_RST   3
#define EPD_BUSY  1
#define EPD_SCL   4  
#define EPD_SDA   6  

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

uint8_t screenRotation = 1; // 90-degree landscape mode

// Mock variables that your future data fetching routines will update
const char* sysTime = "12:45";
const char* batLevel = "84%";
float sp500Change = 2.54;
float nasdaqChange = -0.45;

void drawStockRow(const char* name, float value, int16_t yPos) {
    display.setFont(&FreeSansBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    
    // Draw the index name (e.g., "SP500:")
    display.setCursor(10, yPos);
    display.print(name);
    display.print(": ");

    // Format the percentage text
    char buffer[10];
    snprintf(buffer, sizeof(buffer), "%s%.2f%%", (value >= 0) ? "+" : "", value);

    // Dynamic Styling: Highlight negative drops with a black background badge
    if (value < 0) {
        int16_t tbx, tby; uint16_t tbw, tbh;
        display.getTextBounds(buffer, display.getCursorX(), yPos, &tbx, &tby, &tbw, &tbh);
        
        // Draw a solid background block with padding for the negative drop
        display.fillRect(tbx - 2, tby - 2, tbw + 4, tbh + 4, GxEPD_BLACK);
        display.setTextColor(GxEPD_WHITE); // Invert text to white
    } else {
        display.setTextColor(GxEPD_BLACK);
    }
    
    display.print(buffer);
}

void drawDashboard() {
    display.setRotation(screenRotation);
    display.fillScreen(GxEPD_WHITE);

    // ----------------------------------------------------
    // 1. TOP STATUS BAR
    // ----------------------------------------------------
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);
    
    // Time (Left Corner)
    display.setCursor(5, 20);
    display.print(sysTime);
    
    // Battery (Right Corner - shifting left slightly based on length)
    display.setCursor(150, 20);
    display.print(batLevel);
    
    // Horizontal Separator Line
    display.drawFastHLine(0, 30, 200, GxEPD_BLACK);

    // ----------------------------------------------------
    // 2. FINANCIAL DATA ROWS
    // ----------------------------------------------------
    drawStockRow("SP500", sp500Change, 90);
    drawStockRow("NASDQ", nasdaqChange, 150);
}

void setup() {
    Serial.begin(115200);
    SPI.begin(EPD_SCL, -1, EPD_SDA, EPD_CS); 
    display.init(115200, true, 2, false);
    
    Serial.println("Updating dashboard view...");
    display.setFullWindow();
    display.firstPage();
    do {
        drawDashboard();
    } while (display.nextPage());

    // Put display panels to sleep to preserve hardware lifespan
    display.powerOff();
}

void loop() {
    // Left empty for now
}