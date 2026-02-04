#include "MemoryLCDDisplayManager.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "SerialConfig.h"

#define BLACK 0
#define WHITE 1

MemoryLCDDisplayManager::MemoryLCDDisplayManager(Adafruit_SharpMem& lcdDisplay, U8G2_FOR_ADAFRUIT_GFX& u8g2Display)
    : lcd(lcdDisplay)
    , u8g2(u8g2Display)
    , maxLineWidth(lcd.width())
    , maxLineHeight(lcd.height())
{
  u8g2.begin(lcd);     // connect u8g2 procedures to Adafruit GFX
  u8g2.setFontMode(0);                 // use u8g2 transparent mode (this is default)
  u8g2.setFontDirection(0);            // left to right (this is default)
  u8g2.setForegroundColor(BLACK);      // apply Adafruit GFX color
  u8g2.setBackgroundColor(WHITE);      // apply Adafruit GFX color

  lcd.begin();
  lcd.clearDisplay();
}

MemoryLCDDisplayManager::~MemoryLCDDisplayManager() {
}

void MemoryLCDDisplayManager::drawSplashScreen()
{
  // select u8g2 font from here: https://github.com/olikraus/u8g2/wiki/fntlistall
  u8g2.setFont(u8g2_font_logisoso58_tf);

  const char mercator[] = "MERCATOR";
  const char origins[] = "ORIGINS";
  int16_t width = u8g2.getUTF8Width(mercator);
  int16_t height = 58;
  u8g2.setCursor((maxLineWidth - width)/2,maxLineHeight/2 - height/2);
  u8g2.print(mercator);
  width = u8g2.getUTF8Width(origins);
  u8g2.setCursor((maxLineWidth - width)/2,maxLineHeight/2 + height/2 + 5);
  u8g2.print(origins);

  const char mark[] = "MARK JONES | 2023-2026";
  u8g2.setFont(u8g2_font_logisoso24_tr);
  width = u8g2.getUTF8Width(mark);
  u8g2.setCursor((maxLineWidth - width)/2, maxLineHeight - 10);
  u8g2.print(mark);

  lcd.refresh();
}


void MemoryLCDDisplayManager::clearDisplay() {
    lcd.clearDisplay();
}

void MemoryLCDDisplayManager::drawStatusIndicator(int x, int y, const String& label, bool status, const String& value) {
    
    // Draw label
    u8g2.drawStr(x, y, label.c_str());
    
    // Draw status indicator (✓ or ✗)
    int labelWidth = u8g2.getUTF8Width(label.c_str());
    const char* statusChar = status ? "+" : "-";
    u8g2.drawStr(x + labelWidth + 2, y, statusChar);
    
    // Draw value if provided
    if (value.length() > 0) {
        int statusWidth = u8g2.getUTF8Width(statusChar);
        u8g2.drawStr(x + labelWidth + statusWidth + 4, y, value.c_str());
    }
}

void MemoryLCDDisplayManager::drawIconic(int x_pos, int y_pos, IconicSymbols icon, bool clearBackground)
{
    const int iconWidthHeight = 64;
    const uint8_t* font = u8g2_font_open_iconic_all_8x_t;
    u8g2.setFont(font);

    if (clearBackground || icon == ICON_NONE)
    {
        lcd.fillRect(x_pos, y_pos - iconWidthHeight, iconWidthHeight, iconWidthHeight, WHITE);
    }

    if (icon != ICON_NONE)
        u8g2.drawGlyph(x_pos, y_pos, icon);
}

int MemoryLCDDisplayManager::currentFontHeight()
{
    return u8g2.u8g2.font_info.max_char_height;
}

// Other parameters: Dive Time, Max Depth
void MemoryLCDDisplayManager::displayStatusScreen(
    uint32_t gpsMessagesReceived, uint32_t gpsFixes, uint32_t gpsNoFix,
    uint32_t goodUplinkMessageCount, uint32_t badUplinkMessageCount, bool hasGPSDevice,
    bool hasGPSFix, double gpsHdop, uint8_t gpsSatellites,
    const String& ipAddress, uint32_t mqttUploads, bool wifiConnected,
    const String& wifiSSID, bool dnsConnected, bool ipConnected, bool mqttConnected, uint8_t latestLanternReedState,
    float temperatureLemon, float humidityLemon,
    float temperatureLantern, float humidityLantern, float humidityMako, float depth, 
    float max_depth, int dive_time, const char* lemonUptimeLabel, int gps_hour, int gps_minute, int timezone_offset,
    int max_pipeline_length, int pipeline_interruptions, float powerbank_voltage, bool makoReportsLeak
) 
{
    char lineBuffer[128];

    const int x_icon_offset = 0, x_icon_gap = 8, icon_size = 64;

    // 2 rows, 5 columns of icons 64 pixels wide, 8 pixel gap. 0, 40, 120, 160, 200
    const int col[6] = {x_icon_offset, 
                        x_icon_offset + (x_icon_gap + icon_size) - 1, 
                        x_icon_offset + (x_icon_gap + icon_size) * 2 - 1, 
                        x_icon_offset + (x_icon_gap + icon_size) * 3 - 1, 
                        x_icon_offset + (x_icon_gap + icon_size) * 4 - 1,
                        x_icon_offset + (x_icon_gap + icon_size) * 5 - 1};
    const int row[4] = {icon_size-1, icon_size * 2 - 1, icon_size * 3 - 1, icon_size * 4 - 1};

    lcd.clearDisplayBuffer();

    const int gps_col = 0, gps_row = 0;
    const int internet_col = 1, internet_row = 0;
    const int wifi_col = 0, wifi_row = 1;
    const int mqtt_col = 1, mqtt_row = 1;

    const IconicSymbols gps_icon = ICON_GPS_FIX,            // confirmed works - indoors not fix/fix
                        internet_icon = ICON_WORLD,         // when unblocked MAC - keeps flashing, when blocked starts flashing
                        wifi_icon = ICON_WIFI_SIGNAL,       // confirmed works - block mac address on asus router
                        mqtt_icon = ICON_UP_ARROW_TO_CLOUD; // confirmed works - start/stop mosquitto broker

    static bool gps_icon_show = false, wifi_icon_show = false, internet_icon_show = false, mqtt_icon_show = false;

    gps_icon_show = (hasGPSFix ? true : !gps_icon_show);
    internet_icon_show = (ipConnected ? true : !internet_icon_show);
    wifi_icon_show = (wifiConnected ? true : !wifi_icon_show);
    mqtt_icon_show = (mqttConnected ? true : !mqtt_icon_show);

    drawIconic(col[gps_col],row[gps_row],(gps_icon_show ? gps_icon : ICON_NONE),true);
    drawIconic(col[internet_col],row[internet_row],(internet_icon_show ? internet_icon : ICON_NONE),true);
    drawIconic(col[wifi_col],row[wifi_row],(wifi_icon_show ? wifi_icon : ICON_NONE),true);
    drawIconic(col[mqtt_col],row[mqtt_row]+1,(mqtt_icon_show ? mqtt_icon : ICON_NONE),true);

    const uint8_t* humidityFont = u8g2_font_helvB24_tr;
    const uint8_t* depthFont = u8g2_font_helvB18_tr;
    const uint8_t* wifiFont = u8g2_font_helvB14_tr;

    int text_x = col[2] + (col[1]-col[0])/2 ;
    u8g2.setFont(humidityFont);
    int lineHeight = currentFontHeight();
    int text_y = lineHeight;
        
    // MQTT Status
    drawStatusIndicator(text_x, text_y, "MQTT", mqttConnected, String("  ") + String(mqttUploads));
    text_y += lineHeight;

    // Mako Humidity      
    snprintf(lineBuffer, sizeof(lineBuffer), "Mako     %.0f%%", humidityMako);
    u8g2.drawStr(text_x, text_y, lineBuffer);
    text_y += lineHeight;
 
    // Lemon Humidity
    snprintf(lineBuffer, sizeof(lineBuffer), "Lemon  %.0f%%", humidityLemon);
    u8g2.drawStr(text_x, text_y, lineBuffer);
    text_y += lineHeight;
    
    // Lantern Humidity      
    snprintf(lineBuffer, sizeof(lineBuffer), "Lantern %.0f%%", humidityLantern);
    u8g2.drawStr(text_x, text_y, lineBuffer);


    // 2nd column of text
    text_x = col[0];
    u8g2.setFont(depthFont);
    lineHeight = currentFontHeight();
    text_y = row[2] - (row[2] - row[1])/2;
        
    // Current Depth
    snprintf(lineBuffer, sizeof(lineBuffer), "Depth   %.01f m",depth);
    u8g2.drawStr(text_x, text_y, lineBuffer);
    text_y += lineHeight;

    // Max Depth
    snprintf(lineBuffer, sizeof(lineBuffer), "Max      %.01f m",max_depth);
    u8g2.drawStr(text_x, text_y, lineBuffer);
    text_y += lineHeight;

    // Dive Time - omitted for now
//    dive_time = 99;
 //   snprintf(lineBuffer, sizeof(lineBuffer), "D.Time %d mins",dive_time);
 //   u8g2.drawStr(text_x, text_y, lineBuffer);
 //   text_y += lineHeight;

    // Uptime
    text_x = col[2] + (col[1]-col[0])/2 ;
    text_y = row[2] - (row[2] - row[1])/2;
    snprintf(lineBuffer, sizeof(lineBuffer), "Uptime    %s",lemonUptimeLabel);
    u8g2.drawStr(text_x, text_y, lineBuffer);
    text_y += lineHeight;

    // UTC Time Of Day - ignore timezone_offset for now
    int hour12 = gps_hour % 12;
    if (hour12 == 0)
        hour12 = 12;

    snprintf(lineBuffer, sizeof(lineBuffer), "%.2fV",powerbank_voltage);
    u8g2.drawStr(text_x, text_y, lineBuffer);

    snprintf(lineBuffer, sizeof(lineBuffer),
            "%d:%02d %s",
            hour12,
            gps_minute,
            (gps_hour >= 12 ? "PM" : "AM"));
    text_x = col[4];
    u8g2.drawStr(text_x, text_y, lineBuffer);
    text_y += lineHeight;

    // Set small font
    u8g2.setFont(wifiFont);
    lineHeight = currentFontHeight();
    text_x = col[0];
    text_y = lcd.height()-25;

    // WiFi Status
    drawStatusIndicator(text_x, text_y, "WiFi", wifiConnected, wifiConnected ? wifiSSID : "");
    text_y += lineHeight;

    const char* ipLabel = "NO IP ";
    if (wifiConnected && ipAddress.length() > 0) {
        ipLabel = ipAddress.c_str();
    }

    // IP Address (if connected), Pipe Max Length, Pipe Drain Interruptions, bad uplinks, good uplinks
    snprintf(lineBuffer, sizeof(lineBuffer), "%s P.Max:%d P.Int:%d  %lu/%lu",
                            ipLabel,max_pipeline_length, pipeline_interruptions,badUplinkMessageCount,goodUplinkMessageCount);

    u8g2.drawStr(text_x, text_y, lineBuffer);
    text_y += lineHeight;

    lcd.refresh();       // Send takes 12 millis @ 10 MHz
}

void MemoryLCDDisplayManager::rotateTestDisplay()
{
  testMemoryDisplayCheckerboard();
  testMemoryDisplayu8g2FontsText();
  testMemoryDisplayu8g2FontsIcon();
}

void MemoryLCDDisplayManager::testMemoryDisplayCheckerboard()
{
  const bool calcTimings = false;
  // Total time to clear, draw and refresh the checkboard is ~33 millis @ 10MHz SPI
  const uint32_t memoryLCDUpdateInterval = 3000;
  static uint32_t nextLCDUpdateAt = 0;

  static bool invertPattern = false;
  static unsigned long counter = 0;
    
  if (millis() > nextLCDUpdateAt)
    nextLCDUpdateAt = millis() + memoryLCDUpdateInterval;
  else
    return; // not time yet

  unsigned long clearDisplayStart = micros();
  // Clear screen
  lcd.clearDisplay();   // clear takes 118 micros @ 10MHz
  
  unsigned long drawInBufferStart = micros();

  // Draw 20x20 pixel checkerboard
  const int squareSize = 20;
  
  for (int y = 0; y < lcd.height(); y += squareSize) {
    for (int x = 0; x < lcd.width(); x += squareSize) {
      // Calculate if this square should be filled
      // Checkerboard pattern: (x/size + y/size) % 2
      int xSquare = x / squareSize;
      int ySquare = y / squareSize;
      bool shouldFill = ((xSquare + ySquare) % 2) == 0;
      
      // Invert the pattern on alternate frames
      if (invertPattern) {
        shouldFill = !shouldFill;
      }

      // it's half the time to clear the entire screen then only draw the blacks
      if (shouldFill) {
        // drawing just the blacks takes 21.6 millis
        lcd.fillRect(x, y, squareSize, squareSize, BLACK);
      }
//      else {
//        // drawing the whites also takes 21.6 millis
//        lcd.fillRect(x, y, squareSize, squareSize, WHITE);
//      }
    }
  }
  
  // Refresh display
  unsigned long refreshStart = micros();
  lcd.refresh();          // refresh takes 11 millis @ 10MHz

  if (calcTimings)
  {
    unsigned long clearTime = (drawInBufferStart - clearDisplayStart);
    unsigned long drawInBuffer = (refreshStart - drawInBufferStart);
    unsigned long refreshTime = (micros() - refreshStart);
    
    USB_SERIAL_PRINTF("Memory LCD: Display clears in %lu micros\n", clearTime);
    USB_SERIAL_PRINTF("Memory LCD: Driver draws buffer in %lu micros\n", drawInBuffer);
    USB_SERIAL_PRINTF("Memory LCD: Display refreshed in %lu micros\n", refreshTime);
  }

  // Toggle pattern for next frame
  invertPattern = !invertPattern;
}



void MemoryLCDDisplayManager::testMemoryDisplayu8g2FontsText()
{
  const uint32_t memoryLCDUpdateInterval = 3000;
  static uint32_t nextLCDUpdateAt = millis() + 1000;

  if (millis() > nextLCDUpdateAt)
    nextLCDUpdateAt = millis() + memoryLCDUpdateInterval;
  else
    return; // not time yet

  lcd.clearDisplay();                               // clear the graphcis buffer  
  u8g2.setFont(u8g2_font_logisoso58_tf);  // select u8g2 font from here: https://github.com/olikraus/u8g2/wiki/fntlistall
  u8g2.setCursor(0,100);                // start writing at this position
  u8g2.print(F("Hello World"));
  u8g2.setCursor(0,200);                // start writing at this position
  u8g2.print(F("1234567890"));          
  lcd.refresh();                                    // make everything visible
}

void MemoryLCDDisplayManager::testMemoryDisplayu8g2FontsIcon()
{
  const uint32_t memoryLCDUpdateInterval = 3000;
  static uint32_t nextLCDUpdateAt = millis() + 2000;

  if (millis() > nextLCDUpdateAt)
    nextLCDUpdateAt = millis() + memoryLCDUpdateInterval;
  else
    return; // not time yet

  lcd.clearDisplay();                               // clear the graphcis buffer  
  u8g2.setFont(u8g2_font_open_iconic_all_8x_t);  // select u8g2 font from here: https://github.com/olikraus/u8g2/wiki/fntlistall

  const int iconWidthHeight = 64 + 2;

  int16_t x_pos = 64;
  int16_t y_pos = 64;

  // 400 x 240 display
  uint16_t icon = 100;

  for (int16_t x_pos = 4; x_pos <= maxLineWidth - iconWidthHeight; x_pos += iconWidthHeight)
  {
    for (int16_t y_pos = 68; y_pos <= maxLineHeight; y_pos += iconWidthHeight)
    {
      u8g2.drawGlyph(x_pos, y_pos, icon);
      icon++;
    }
  }

  lcd.refresh();                                    // make everything visible
}

