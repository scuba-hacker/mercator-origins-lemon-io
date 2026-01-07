#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
//#include <Adafruit_SSD1327.h>

class OLEDWideDisplayManager {
public:
    // Reference to the U8G2 display object
    U8G2& display;
    
    // Constructor
    OLEDWideDisplayManager(U8G2& u8g2Display, int screenWidth = 256, int screenHeight = 64, int maxLines = 4);
    
    // Destructor
    ~OLEDWideDisplayManager();
    
    // Multi-line scrolling display methods
    void addDisplayLine(const String& newLine, bool preserveWiFiLine = false, bool skipRefresh = false);
    void refreshDisplay();
    
    // Single-line scrolling status methods
    void updateScrollingStatusLine(const String& newText, bool append = true, bool scrollOffPrevious = false, int yPosition = 55);
    
    // Progress animation methods
    void startProgressAnimation();
    void stopProgressAnimation();
    void updateProgressAnimation(int yPosition = 55);

    // Set screensaver period
    void setScreenSaverPeriod(uint32_t ms);
    
    // Status display methods


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

    void displayStatusScreenTextOnly(
        uint32_t gpsMessagesReceived, uint32_t gpsFixes, uint32_t gpsNoFix,
        uint32_t gpsBadChecksum, uint32_t gpsBadLength, bool hasGPSDevice,
        bool hasGPSFix, double gpsHdop, uint8_t gpsSatellites,
        const String& ipAddress, uint32_t mqttUploads, bool wifiConnected,
        const String& wifiSSID, bool dnsConnected, bool ipConnected, bool mqttConnected, uint8_t latestLanternReedState,
        float temperatureLemon, float humidityLemon,
        float temperatureLantern, float humidityLantern, float humidityMako, float depth, 
        float max_depth, int dive_time, const char* lemonUptimeLabel,  int gps_hour, int gps_minute, int timezone_offset,
        int max_pipeline_length, int pipeline_backups, float powerbank_voltage, bool makoReportsLeak
    );
    
    void setStatusDisplayMode(bool enabled);
    bool isInStatusDisplayMode() const { return statusDisplayModeActive; }
    
    // Utility methods
    void clearDisplay();
    void setOTAMode(bool enabled);
    bool isInOTAMode() const { return otaModeActive; }
    bool isShowingProgress() const { return showingProgress; }
    int getCurrentLineCount() const { return currentLineCount; }
    String getCurrentScrollingStatusLine() const { return scrollingStatusLine; }

    void enableScreenSaver(bool enable) 
    { 
        if (!enable)
            displayRootX = displayRootY = 0;

        screenSaverEnabled = enable; 
    }

private:
    int displayRootX = 0;
    int displayRootY = 0;
    const int maxOffsetX = 30;
    const int maxOffsetY = 20;
    const int screenSaverStep = 7;
    int screenSaverStepDirectionX = 1;
    int screenSaverStepDirectionY = 1;
    uint32_t screenSaverPeriod = 5000;
    bool screenSaverEnabled;
    uint32_t nextScreenShift = screenSaverPeriod; 

    void shiftScreen();

    // Generic scrolling status line variables
    String scrollingStatusLine;
    String baseStatusLine;  // Base line without progress chars
    int scrollOffset;
    const int maxLineWidth;  // Full screen width
    const int maxLineHeight; // Full screen height
    bool showingProgress;
    int progressCharCount;
    
    // Display scrolling system variables
    const int maxDisplayLines;  // Max number of lines in multi-line display
    String* displayLines;
    int currentLineCount;
    
    // OTA mode flag
    bool otaModeActive;
    
    // Status display mode flag
    bool statusDisplayModeActive;
    
    // Private helper methods
    void updateScrollingStatusLineDisplay(int yPosition);
    void drawStatusIndicator(int x, int y, const String& label, bool status, const String& value = "");
    u8g2_uint_t safeDrawStr(u8g2_uint_t x, u8g2_uint_t y, const char *s);
};