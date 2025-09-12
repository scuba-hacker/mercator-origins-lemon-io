#!/usr/bin/env python3
"""
Convert stats_html.html to stats_html.c
Usage: python3 convert_stats_html.py
"""

import os
import sys

def convert_stats_html():
    # Paths relative to project root
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    html_file = os.path.join(project_root, "src", "stats_html.html")
    c_file = os.path.join(project_root, "src", "stats_html.c")
    
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
    
    # Generate C file content
    c_content = f"""#include <stdint.h>

#ifndef STATS_HTML_C
#define STATS_HTML_C

const char STATS_HTML[] = R"rawliteral(
{html_content})rawliteral";

const uint32_t STATS_HTML_SIZE = sizeof(STATS_HTML);

#endif
"""
    
    # Write C file
    try:
        with open(c_file, 'w', encoding='utf-8') as f:
            f.write(c_content)
        print(f"Successfully converted {html_file} -> {c_file}")
        return True
    except Exception as e:
        print(f"Error writing C file: {e}")
        return False

if __name__ == "__main__":
    success = convert_stats_html()
    sys.exit(0 if success else 1)