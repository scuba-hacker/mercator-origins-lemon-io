#include "OLEDLXDisplayManager.h"
#include <LGFX_Adafruit_SSD1327.h>

OLEDLXDisplayManager::OLEDLXDisplayManager(LGFX_I2C_Adafruit_SSD1327_128x128_Grey_OLE& lgfx_adaFruitDisplay)
    : display(lgfx_adaFruitDisplay)
{
}

OLEDLXDisplayManager::~OLEDLXDisplayManager() 
{
}

void OLEDLXDisplayManager::clearDisplay() 
{
  display.clearDisplay();
  display.display();
}

void OLEDLXDisplayManager::begin()
{
  display.begin();
  display.init();
}

void OLEDLXDisplayManager::rotatedGrayBarTest()
{
  static uint32_t count = 0;

  display.setRotation(0);
  display.setTextSize((std::max(display.width(), display.height()) + 255) >> 8);

  uint32_t endOfTest = millis() + 5000;
  while (millis() < endOfTest)
  {
    display.startWrite();

    // Cycle through rotation modes 0–3 (assuming SSD1327 supports them)
    display.setRotation(count & 3);

    display.setTextColor(0xFFFFFF,0); // white
    display.drawNumber(display.getRotation(), 16, 0);

    display.drawString("R", 30, 36);
    display.drawString("G", 40, 36);
    display.drawString("B", 50, 36);

    // Draw animated rectangle using grayscale pattern
    display.drawRect(30, 30, display.width() - 60, display.height() - 60, (count % 16));

    // Simple line for debugging
    display.drawFastHLine(0, 0, 10);

    display.endWrite();

    count++;

    for (int i = 0; i < 128; ++i)
    {
      uint8_t j = (i * 255) / 127;              // j = 0 to 255, inclusive
      uint32_t k = (j << 16) | (j << 8) | j;    // RGB = (j, j, j) → grayscale color
      display.fillRect(i, 0, 1, 20, k);                 // Draw vertical 1px wide bar
    }

    delay(500); // add delay to visualize changes
  }
}