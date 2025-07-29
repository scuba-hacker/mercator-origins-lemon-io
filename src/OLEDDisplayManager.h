#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include <Adafruit_SSD1327.h>

class OLEDDisplayManager {
public:
    // Reference to the U8G2 display object
    U8G2& display;
    
    // Constructor
    OLEDDisplayManager(U8G2& u8g2Display, int screenWidth = 256, int maxLines = 4);
    
    // Destructor
    ~OLEDDisplayManager();
    
    // Multi-line scrolling display methods
    void addDisplayLine(const String& newLine, bool preserveWiFiLine = false, bool skipRefresh = false);
    void refreshDisplay();
    
    // Single-line scrolling status methods
    void updateScrollingStatusLine(const String& newText, bool append = true, bool scrollOffPrevious = false, int yPosition = 55);
    
    // Progress animation methods
    void startProgressAnimation();
    void stopProgressAnimation();
    void updateProgressAnimation(int yPosition = 55);
    
    // Status display methods
    void displayStatusScreen(
        // GPS statistics
        uint32_t gpsMessagesReceived, uint32_t gpsFixes, uint32_t gpsNoFix,
        uint32_t gpsBadChecksum, uint32_t gpsBadLength, bool hasGPSDevice,
        bool hasGPSFix, double gpsHdop, uint8_t gpsSatellites,
        // Network statistics  
        const String& ipAddress, uint32_t mqttUploads, bool wifiConnected,
        const String& wifiSSID, bool dnsConnected, bool ipConnected, bool mqttConnected
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

private:
    // Generic scrolling status line variables
    String scrollingStatusLine;
    String baseStatusLine;  // Base line without progress chars
    int scrollOffset;
    const int maxLineWidth;  // Full screen width
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
};