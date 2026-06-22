#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>

#define EPD_CS    10
#define EPD_DC    2
#define EPD_RST   3
#define EPD_BUSY  1
#define EPD_SCL   4  
#define EPD_SDA   6  

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Bounding box variables for our dynamic text area
int16_t  startX = 20;  // X coordinate where the name starts drawing
int16_t  startY = 110; // Y coordinate (baseline of the font)
uint16_t boxW   = 160; // Width of the partial refresh box
uint16_t boxH   = 30;  // Height of the partial refresh box

void helloWorld() {
    display.setRotation(1);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(20, 60);
        display.print("Hello");
        
        display.setCursor(startX, startY);
        display.print("World!");
    } while (display.nextPage());
}

void updateName(const char* newName) {
    display.setRotation(1);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);

    // 1. Define the tiny box on the screen we want to alter
    // setPartialWindow(x, y, width, height)
    display.setPartialWindow(startX, startY - 20, boxW, boxH); 
    
    // 2. Execute the partial refresh loop
    display.firstPage();
    do {
        // Clear ONLY our partial window box with white
        display.fillRect(startX, startY - 20, boxW, boxH, GxEPD_WHITE);
        
        // Write the new text into the box
        display.setCursor(startX, startY);
        display.print(newName);
    } while (display.nextPage());
}

void setup() {
    Serial.begin(115200);
    SPI.begin(EPD_SCL, -1, EPD_SDA, EPD_CS); 
    display.init(115200, true, 2, false);
    
    Serial.println("Drawing baseline Hello World...");
    helloWorld();
    
    delay(3000); // Wait 3 seconds so you can see the original text
    
    Serial.println("Performing partial refresh to 'Mathias!'...");
    updateName("Mathias!");
    
    // Keep power on if you plan to do more quick updates, 
    // but we power down here for best practice.
    display.powerOff(); 
}

void loop() {
    // Empty
}