#!/bin/bash
# Convert logs_page.html to logs_page.h
# Usage: ./tools/update_html.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "Converting HTML to header..."
python3 "$SCRIPT_DIR/html_to_header.py"

if [ $? -eq 0 ]; then
    echo "✓ HTML successfully converted to header file"
else
    echo "✗ Error converting HTML file"
    exit 1
fi