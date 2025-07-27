#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

class OLEDDisplayManager {
private:
    // Reference to the U8G2 display object
    U8G2& display;
    
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
    
    // Private helper methods
    void updateScrollingStatusLineDisplay(int yPosition);

public:
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
    
    // Utility methods
    void clearDisplay();
    bool isShowingProgress() const { return showingProgress; }
    int getCurrentLineCount() const { return currentLineCount; }
    String getCurrentScrollingStatusLine() const { return scrollingStatusLine; }
};