#pragma once

#include <Arduino.h>
class Adafruit_SSD1327;

class OLEDGSDisplayManager 
{

private:

public:
    // Reference to the Adafruit_SSD1327 display object
    Adafruit_SSD1327& display;
    
    // Constructor
    OLEDGSDisplayManager(Adafruit_SSD1327& adafruitDisplay);
    
    // Destructor
    ~OLEDGSDisplayManager();
    
    void clearDisplay();

    void fullDisplayTest();
    void drawAFewSnowflakes();
    void testdrawbitmap(const uint8_t *bitmap, uint8_t w, uint8_t h, uint32_t maxFrames=0);
    void testdrawchar();
    void testdrawcircle();
    void testfillrect();
    void testdrawrect();
    void testdrawtriangle();
    void testfilltriangle();
    void testdrawroundrect();
    void testfillroundrect();
    void testdrawline();
};