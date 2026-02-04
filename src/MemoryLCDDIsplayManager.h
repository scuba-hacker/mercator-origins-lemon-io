#pragma once

#include <Arduino.h>

class U8G2_FOR_ADAFRUIT_GFX;
class Adafruit_SharpMem;

class MemoryLCDDisplayManager {
public:
    // Reference to the U8G2 display object
    Adafruit_SharpMem& lcd;
    U8G2_FOR_ADAFRUIT_GFX& u8g2;
    
    // Constructor
    MemoryLCDDisplayManager(Adafruit_SharpMem& lcdDisplay, U8G2_FOR_ADAFRUIT_GFX& u8g2Display);
    
    // Destructor
    ~MemoryLCDDisplayManager();
        
    void drawSplashScreen();

    void displayStatusScreen(
        // GPS statistics
        uint32_t gpsMessagesReceived, uint32_t gpsFixes, uint32_t gpsNoFix,
        uint32_t goodUplinkMessageCount, uint32_t badUplinkMessageCount, bool hasGPSDevice,
        bool hasGPSFix, double gpsHdop, uint8_t gpsSatellites,
        // Network statistics  
        const String& ipAddress, uint32_t mqttUploads, bool wifiConnected,
        const String& wifiSSID, bool dnsConnected, bool ipConnected, bool mqttConnected, uint8_t latestLanternReedState,
        float temperatureLemon, float humidityLemon,
        float temperatureLantern, float humidityLantern, float humidityMako, float depth, 
        float max_depth, int dive_time, const char* lemonUptimeLabel,  int gps_hour, int gps_minute, int timezone_offset,
        int max_pipeline_length, int pipeline_backups, float powerbank_voltage, bool makoReportsLeak
    );

    // Utility methods
    void clearDisplay();

    // Test methods
    void rotateTestDisplay();
    void testMemoryDisplayCheckerboard();
    void testMemoryDisplayu8g2FontsIcon();
    void testMemoryDisplayu8g2FontsText();

private:
    // open_iconic_all_4x
    // 32 pixels high
    enum IconicSymbols {
        ICON_NONE = 0,
        ICON_ANTENNA_BROADCAST = 84,
        ICON_COMPASS = 136,
        ICON_GPS_FIX = 201,
        ICON_HORIZ_ARROWS = 270,
        ICON_UP_ARROW_TO_CLOUD = 126,
        ICON_WIFI_ALT = 282,
        ICON_WIFI_SIGNAL = 248,      // or 249
        ICON_WORLD = 175,
        ICON_X_BIG = 284,
        ICON_X_ROUNDEL = 122   // or 303
    };

    const int maxLineWidth;  // Full screen width
    const int maxLineHeight; // Full screen height
    
    void drawStatusIndicator(int x, int y, const String& label, bool status, const String& value = "");
    void drawIconic(int x_pos, int y_pos, IconicSymbols icon, bool clearBackground);

    int currentFontHeight();
};