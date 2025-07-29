#include "OLEDDisplayManager.h"

OLEDWideDisplayManager::OLEDWideDisplayManager(U8G2& u8g2Display, int screenWidth, int maxLines)
    : display(u8g2Display)
    , scrollingStatusLine("")
    , baseStatusLine("")
    , scrollOffset(0)
    , maxLineWidth(screenWidth)
    , showingProgress(false)
    , progressCharCount(0)
    , maxDisplayLines(maxLines)
    , currentLineCount(0)
    , otaModeActive(false)
    , statusDisplayModeActive(false)
    , screenSaverEnabled(true)
{
    displayLines = new String[maxDisplayLines];
}

OLEDWideDisplayManager::~OLEDWideDisplayManager() {
    delete[] displayLines;
}

void OLEDWideDisplayManager::setScreenSaverPeriod(uint32_t ms)
{
    screenSaverPeriod = ms;
}

void OLEDWideDisplayManager::shiftScreen()
{
    if (screenSaverEnabled && millis() > nextScreenShift)
    {
        displayRootX += random(1, screenSaverStep) * screenSaverStepDirectionX;
        displayRootY += random(1, screenSaverStep) * screenSaverStepDirectionY;
        
        if (displayRootX < 0)
        {
            displayRootX = random(1, screenSaverStep);;
            screenSaverStepDirectionX *= -1;
        }
        else if (displayRootX > maxOffsetX)
        {
            displayRootX = maxOffsetX - random(1, screenSaverStep);
            screenSaverStepDirectionX *= -1;
        }

        if (displayRootY < 0)
        {
            displayRootY = random(1, screenSaverStep);;
            screenSaverStepDirectionY *= -1;
        }
        else if (displayRootY > maxOffsetY)
        {
            displayRootY = maxOffsetY - random(1, screenSaverStep);
            screenSaverStepDirectionY *= -1;
        }

        nextScreenShift += screenSaverPeriod;
    }
    else
    {
        displayRootX=0;
        displayRootY=0;
    }   
}

void OLEDWideDisplayManager::startProgressAnimation() {
    showingProgress = true;
    progressCharCount = 0;
    baseStatusLine = scrollingStatusLine;  // Save current line as base
}

void OLEDWideDisplayManager::stopProgressAnimation() {
    showingProgress = false;
    progressCharCount = 0;
    scrollingStatusLine = baseStatusLine;  // Restore base line without progress chars
}

void OLEDWideDisplayManager::updateProgressAnimation(int yPosition) {
    if (!showingProgress) return;

    const int maxDotCount = 5;      // Maximum of 5 dots
    // Add progress dots (up to maxDotCount, then cycle)
    progressCharCount = (progressCharCount + 1) % (maxDotCount + 1);
    
    // Build progress string - ensure consistent width to overwrite previous dots
    char progressDots[] = "            ";  // Start with 12 spaces to handle up to 10 dots (2 spaces + 8 dots)
    for (int i = 0; i < progressCharCount; i++) 
    {
        progressDots[2 + i] = '.';  // Place dots starting at position 2
    }
    
    // Update the status line with progress
    scrollingStatusLine = baseStatusLine + progressDots;
    
    // Update display at specified Y position
    updateScrollingStatusLineDisplay(yPosition);
}

void OLEDWideDisplayManager::addDisplayLine(const String& newLine, bool preserveWiFiLine, bool skipRefresh) {
    // Block all display updates during OTA mode
    if (otaModeActive) {
        return;
    }
    
    if (currentLineCount < maxDisplayLines) {
        // Still have room, just add the line
        displayLines[currentLineCount] = newLine;
        currentLineCount++;
    } else {
        // Scroll up: shift all lines up by one
        for (int i = 0; i < maxDisplayLines - 1; i++) {
            displayLines[i] = displayLines[i + 1];
        }
        // Add new line at bottom
        displayLines[maxDisplayLines - 1] = newLine;
    }
    
    // Redraw all lines (unless skipRefresh is true)
    if (!skipRefresh) {
        refreshDisplay();
    }
}


u8g2_uint_t OLEDWideDisplayManager::safeDrawStr(u8g2_uint_t x, u8g2_uint_t y, const char *s) 
{ 
    return display.drawStr(x + displayRootX, y + displayRootY, s);
}

void OLEDWideDisplayManager::refreshDisplay() {
    display.setFont(u8g2_font_ncenB08_tr);
    
    // Clear the display
    display.setDrawColor(0);  // Black (erase)
    display.drawBox(0, 0, maxLineWidth, 64);  // Clear entire display
    display.setDrawColor(1);  // White (draw)
    
    // Draw all current lines
    for (int i = 0; i < currentLineCount; i++) {
        int yPos = 10 + (i * 15);  // 15 pixels between lines
        safeDrawStr(0, yPos, displayLines[i].c_str());
    }
    
    display.sendBuffer();
}

void OLEDWideDisplayManager::updateScrollingStatusLineDisplay(int yPosition) {
    display.setFont(u8g2_font_ncenB08_tr);
    int textWidth = display.getUTF8Width(scrollingStatusLine.c_str());
    
    // Clear the status line area - make sure to clear entire width to remove old text
    display.setDrawColor(0);  // Black (erase)
    display.drawBox(0, yPosition-8, maxLineWidth, 10);
    display.setDrawColor(1);  // White (draw)
    
    // If text fits on screen, display normally
    if (textWidth <= maxLineWidth) {
        safeDrawStr(0, yPosition, scrollingStatusLine.c_str());
        scrollOffset = 0;
    } else {
        // Text is too long, need to scroll to show the end
        int targetScrollOffset = textWidth - maxLineWidth + 10;  // +10 for small margin
        if (scrollOffset < targetScrollOffset) {
            scrollOffset = targetScrollOffset;  // Jump to end position for progress display
        }
        safeDrawStr(-scrollOffset, yPosition, scrollingStatusLine.c_str());
    }
    
    display.sendBuffer();
}

void OLEDWideDisplayManager::updateScrollingStatusLine(const String& newText, bool append, bool scrollOffPrevious, int yPosition) {
    // Block all scrolling status updates during OTA mode
    if (otaModeActive) {
        return;
    }
    
    int pixelScrollDelay = 2;
    
    // Stop any progress animation when updating text
    if (showingProgress) {
        stopProgressAnimation();
    }
    
    if (append) {
        if (scrollingStatusLine.length() > 0) {
            scrollingStatusLine += " -> " + newText;
        } else {
            scrollingStatusLine = newText;
        }
    } else {
        scrollingStatusLine = newText;
        scrollOffset = 0;  // Reset scroll when replacing text
    }
    
    // Calculate text width
    display.setFont(u8g2_font_ncenB08_tr);
    int textWidth = display.getUTF8Width(scrollingStatusLine.c_str());
    
    if (scrollOffPrevious && append) {
        // Special mode: scroll off all previous text, leaving only the new message visible
        // Calculate where the new message starts in the full string
        int newMessageWidth = display.getUTF8Width(newText.c_str());
        int targetScrollOffset = textWidth - newMessageWidth;
        
        // Smooth scroll to push previous text off screen
        while (scrollOffset < targetScrollOffset) {
            // Clear the status line area
            display.setDrawColor(0);  // Black (erase)
            display.drawBox(0, yPosition-8, maxLineWidth, 10);
            display.setDrawColor(1);  // White (draw)
            
            // Draw the text with current offset
            safeDrawStr(-scrollOffset, yPosition, scrollingStatusLine.c_str());
            display.sendBuffer();
            
            scrollOffset += 1;  // Scroll by 1 pixel at a time
            delay(pixelScrollDelay);
        }
    } else {
        // Normal behavior - scroll to show end of text
        // If text fits on screen, display normally
        if (textWidth <= maxLineWidth) {
            // Clear the status line area
            display.setDrawColor(0);  // Black (erase)
            display.drawBox(0, yPosition-8, maxLineWidth, 10);
            display.setDrawColor(1);  // White (draw)
            
            safeDrawStr(0, yPosition, scrollingStatusLine.c_str());
            display.sendBuffer();
            scrollOffset = 0;
        } else {
            // Text is too long, need to scroll to show the end
            int targetScrollOffset = textWidth - maxLineWidth + 10;  // +10 for small margin
            
            // Smooth scroll to target position
            while (scrollOffset < targetScrollOffset) {
                // Clear the status line area
                display.setDrawColor(0);  // Black (erase)
                display.drawBox(0, yPosition-8, maxLineWidth, 10);
                display.setDrawColor(1);  // White (draw)
                
                // Draw the text with current offset
                safeDrawStr(-scrollOffset, yPosition, scrollingStatusLine.c_str());
                display.sendBuffer();
                
                scrollOffset += 1;  // Scroll by 1 pixel at a time
                delay(pixelScrollDelay);
            }
        }
    }
}

void OLEDWideDisplayManager::clearDisplay() {
    display.setDrawColor(0);  // Black (erase)
    display.drawBox(0, 0, maxLineWidth, 64);  // Clear entire display
    display.setDrawColor(1);  // White (draw)
    display.sendBuffer();
    
    // Reset state
    currentLineCount = 0;
    scrollingStatusLine = "";
    baseStatusLine = "";
    scrollOffset = 0;
    showingProgress = false;
    progressCharCount = 0;
}

void OLEDWideDisplayManager::setOTAMode(bool enabled) {
    otaModeActive = enabled;
}

void OLEDWideDisplayManager::setStatusDisplayMode(bool enabled) {
    statusDisplayModeActive = enabled;
}

void OLEDWideDisplayManager::drawStatusIndicator(int x, int y, const String& label, bool status, const String& value) {
    display.setFont(u8g2_font_4x6_tr);
    
    // Draw label
    safeDrawStr(x, y, label.c_str());
    
    // Draw status indicator (✓ or ✗)
    int labelWidth = display.getUTF8Width(label.c_str());
    const char* statusChar = status ? "+" : "-";
    safeDrawStr(x + labelWidth + 2, y, statusChar);
    
    // Draw value if provided
    if (value.length() > 0) {
        int statusWidth = display.getUTF8Width(statusChar);
        safeDrawStr(x + labelWidth + statusWidth + 4, y, value.c_str());
    }
}
/*
Tests to do mid-way through system operation: (ie not at boot)
- check for recovery of system
- check for good display updates

1. WORKS: Pull the GPS pin out of the board - shows NO GPS DEVICE correctly. Layer 0 - GPS Hardware
        GPS processing starts/stops and continues with upload to MQTT Broker.
        Display updates are correct.

2. WORKS: Stop MQTT broker. works (+/- updates) - App Layer
        Bathroom-DietPi# systemctl stop mosquitto
        Bathroom-DietPi# systemctl start mosquitto

3. TO DO: Use Asus-Router to block 192.168.0.58 Lemon from reaching anywhere - simulate no WiFi router, eg WiFi hotspot powered down.
        - then revert and check ok

4. TO DO: Use a rule on asus-router to block an IP address - simulate no WiFi network connectivity - ie SIM/LTE out of range.
        - then revert and check ok
        
5. DONE: DNS Resolution failure and re-establish - used Pi-Hole block rules for google.com, the name it tests.

6. TO DO: Change wifi password to break authentication.

7. TO DO: Change known wifi networks not in range.

8. TO DO: check that bounce of WiFi Hub there is automatic reconnection of WiFi.

*/
void OLEDWideDisplayManager::displayStatusScreen(
    uint32_t gpsMessagesReceived, uint32_t gpsFixes, uint32_t gpsNoFix,
    uint32_t gpsBadChecksum, uint32_t gpsBadLength, bool hasGPSDevice,
    bool hasGPSFix, double gpsHdop, uint8_t gpsSatellites,
    const String& ipAddress, uint32_t mqttUploads, bool wifiConnected,
    const String& wifiSSID, bool dnsConnected, bool ipConnected, bool mqttConnected) {
    
    // Block status display updates during OTA mode
    if (otaModeActive) {
        return;
    }
    
    shiftScreen();  // screen saver
    
    // Clear display
    display.setDrawColor(0);
    display.drawBox(0, 0, maxLineWidth, 64);
    display.setDrawColor(1);
    
    // Left column (GPS info)
    int leftX = 0;
    int rightX = 130;
    int y = 8;
    int lineHeight = 8;
    
    display.setFont(u8g2_font_4x6_tr);
    
    // GPS Device Status
    if (!hasGPSDevice) {
        safeDrawStr(leftX, y, "NO GPS DEVICE");
        y += lineHeight;
    } else {
        String gpsStatus = hasGPSFix ? "GPS FIX" : "NO FIX";
        drawStatusIndicator(leftX, y, "GPS", hasGPSFix, gpsStatus);
        y += lineHeight;
        
        // GPS message statistics
        safeDrawStr(leftX, y, ("MSG:" + String(gpsMessagesReceived)).c_str());
        y += lineHeight;
        
        safeDrawStr(leftX, y, ("FIX:" + String(gpsFixes)).c_str());
        y += lineHeight;
        
        if (gpsBadChecksum > 0 || gpsBadLength > 0) {
            safeDrawStr(leftX, y, ("ERR:" + String(gpsBadChecksum + gpsBadLength)).c_str());
            y += lineHeight;
        }
        
        // GPS quality
        if (hasGPSFix) {
            safeDrawStr(leftX, y, ("SAT:" + String(gpsSatellites)).c_str());
            y += lineHeight;
            
            safeDrawStr(leftX, y, ("HDOP:" + String(gpsHdop, 1)).c_str());
        }
    }
    
    // Right column (Network info)
    y = 8;
    
    // WiFi Status
    drawStatusIndicator(rightX, y, "WiFi", wifiConnected, wifiConnected ? wifiSSID : "");
    y += lineHeight;
    
    // DNS connectivity
    drawStatusIndicator(rightX, y, "DNS", dnsConnected);
    y += lineHeight;
    
    // IP connectivity
    drawStatusIndicator(rightX, y, "IP", ipConnected);
    y += lineHeight;
    
    // MQTT Status
    drawStatusIndicator(rightX, y, "MQTT", mqttConnected, String(mqttUploads));
    y += lineHeight;
    
    // IP Address (if connected)
    if (wifiConnected && ipAddress.length() > 0) {
        safeDrawStr(rightX, y, ipAddress.c_str());
    }
    
    display.sendBuffer();
}
