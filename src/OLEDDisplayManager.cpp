#include "OLEDDisplayManager.h"

OLEDDisplayManager::OLEDDisplayManager(U8G2& u8g2Display, int screenWidth, int maxLines)
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
{
    displayLines = new String[maxDisplayLines];
}

OLEDDisplayManager::~OLEDDisplayManager() {
    delete[] displayLines;
}

void OLEDDisplayManager::startProgressAnimation() {
    showingProgress = true;
    progressCharCount = 0;
    baseStatusLine = scrollingStatusLine;  // Save current line as base
}

void OLEDDisplayManager::stopProgressAnimation() {
    showingProgress = false;
    progressCharCount = 0;
    scrollingStatusLine = baseStatusLine;  // Restore base line without progress chars
}

void OLEDDisplayManager::updateProgressAnimation(int yPosition) {
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

void OLEDDisplayManager::addDisplayLine(const String& newLine, bool preserveWiFiLine, bool skipRefresh) {
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

void OLEDDisplayManager::refreshDisplay() {
    display.setFont(u8g2_font_ncenB08_tr);
    
    // Clear the display
    display.setDrawColor(0);  // Black (erase)
    display.drawBox(0, 0, maxLineWidth, 64);  // Clear entire display
    display.setDrawColor(1);  // White (draw)
    
    // Draw all current lines
    for (int i = 0; i < currentLineCount; i++) {
        int yPos = 10 + (i * 15);  // 15 pixels between lines
        display.drawStr(0, yPos, displayLines[i].c_str());
    }
    
    display.sendBuffer();
}

void OLEDDisplayManager::updateScrollingStatusLineDisplay(int yPosition) {
    display.setFont(u8g2_font_ncenB08_tr);
    int textWidth = display.getUTF8Width(scrollingStatusLine.c_str());
    
    // Clear the status line area - make sure to clear entire width to remove old text
    display.setDrawColor(0);  // Black (erase)
    display.drawBox(0, yPosition-8, maxLineWidth, 10);
    display.setDrawColor(1);  // White (draw)
    
    // If text fits on screen, display normally
    if (textWidth <= maxLineWidth) {
        display.drawStr(0, yPosition, scrollingStatusLine.c_str());
        scrollOffset = 0;
    } else {
        // Text is too long, need to scroll to show the end
        int targetScrollOffset = textWidth - maxLineWidth + 10;  // +10 for small margin
        if (scrollOffset < targetScrollOffset) {
            scrollOffset = targetScrollOffset;  // Jump to end position for progress display
        }
        display.drawStr(-scrollOffset, yPosition, scrollingStatusLine.c_str());
    }
    
    display.sendBuffer();
}

void OLEDDisplayManager::updateScrollingStatusLine(const String& newText, bool append, bool scrollOffPrevious, int yPosition) {
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
            display.drawStr(-scrollOffset, yPosition, scrollingStatusLine.c_str());
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
            
            display.drawStr(0, yPosition, scrollingStatusLine.c_str());
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
                display.drawStr(-scrollOffset, yPosition, scrollingStatusLine.c_str());
                display.sendBuffer();
                
                scrollOffset += 1;  // Scroll by 1 pixel at a time
                delay(pixelScrollDelay);
            }
        }
    }
}

void OLEDDisplayManager::clearDisplay() {
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

void OLEDDisplayManager::setOTAMode(bool enabled) {
    otaModeActive = enabled;
}