#pragma once

#include <Arduino.h>
class LGFX_I2C_Adafruit_SSD1327_128x128_Grey_OLE;

class OLEDLXDisplayManager 
{

private:

public:
    LGFX_I2C_Adafruit_SSD1327_128x128_Grey_OLE& display;
    
    OLEDLXDisplayManager(LGFX_I2C_Adafruit_SSD1327_128x128_Grey_OLE& adafruitDisplay);
    ~OLEDLXDisplayManager();
    
    void begin();

    void rotatedGrayBarTest();
    void clearDisplay();
};