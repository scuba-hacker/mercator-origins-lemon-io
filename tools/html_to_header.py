#!/usr/bin/env python3
"""
Convert HTML file to C++ header with embedded string
Usage: python3 html_to_header.py
"""

import os
import sys

def convert_html_to_header():
    # Paths relative to project root
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    html_file = os.path.join(project_root, "src", "logs_page.html")
    header_file = os.path.join(project_root, "src", "logs_page.h")
    
    # Check if HTML file exists
    if not os.path.exists(html_file):
        print(f"Error: {html_file} not found")
        return False
    
    # Read HTML content
    try:
        with open(html_file, 'r', encoding='utf-8') as f:
            html_content = f.read()
    except Exception as e:
        print(f"Error reading HTML file: {e}")
        return False
    
    # Generate header content
    header_content = f"""#pragma once

const char LOGS_PAGE_HTML[] PROGMEM = R"rawliteral(
{html_content})rawliteral";
"""
    
    # Write header file
    try:
        with open(header_file, 'w', encoding='utf-8') as f:
            f.write(header_content)
        print(f"Successfully converted {html_file} -> {header_file}")
        return True
    except Exception as e:
        print(f"Error writing header file: {e}")
        return False

if __name__ == "__main__":
    success = convert_html_to_header()
    sys.exit(0 if success else 1)