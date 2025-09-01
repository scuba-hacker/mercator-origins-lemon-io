#pragma once

const char LOGS_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Lemon IO - Serial Console</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: monospace; 
            margin: 0; 
            padding: 20px; 
            background: #1e1e1e; 
            color: #d4d4d4; 
        }
        .container { 
            max-width: 1200px; 
            margin: 0 auto; 
        }
        h1 { 
            color: #569cd6; 
            text-align: center; 
            margin-bottom: 20px; 
        }
        .controls { 
            margin-bottom: 20px; 
            text-align: center; 
        }
        .command-controls {
            margin-bottom: 20px;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .command-input {
            flex: 1;
            padding: 8px;
            font-family: monospace;
            border: 1px solid #444;
            background: #2d2d2d;
            color: #d4d4d4;
            border-radius: 4px;
        }
        .command-dropdown {
            padding: 8px;
            font-family: monospace;
            border: 1px solid #444;
            background: #2d2d2d;
            color: #d4d4d4;
            border-radius: 4px;
            min-width: 150px;
        }
        button { 
            background: #0078d4; 
            color: white; 
            border: none; 
            padding: 10px 20px; 
            margin: 0 10px; 
            border-radius: 4px; 
            cursor: pointer; 
            font-family: monospace; 
        }
        button:hover { 
            background: #106ebe; 
        }
        button:disabled { 
            background: #666; 
            cursor: not-allowed; 
        }
        #console { 
            background: #000; 
            border: 1px solid #444; 
            padding: 10px; 
            height: 500px; 
            overflow-y: auto; 
            font-size: 14px; 
            line-height: 1.4; 
            white-space: pre-wrap; 
            word-wrap: break-word; 
        }
        .status { 
            margin-top: 10px; 
            padding: 10px; 
            text-align: center; 
            border-radius: 4px; 
        }
        .connected { 
            background: #164e24; 
            color: #4fc3f7; 
        }
        .disconnected { 
            background: #4e1616; 
            color: #f44336; 
        }
        .timestamp { 
            color: #999; 
        }
        .ota-banner {
            background: #ff6b35;
            color: white;
            padding: 15px;
            text-align: center;
            border-radius: 4px;
            margin-bottom: 20px;
            font-weight: bold;
            animation: pulse 1s infinite;
        }
        @keyframes pulse {
            0% { opacity: 1; }
            50% { opacity: 0.7; }
            100% { opacity: 1; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Lemon IO - Serial Console</h1>
        <div class="controls">
            <button onclick="clearConsole()">Clear</button>
            <button onclick="saveConsole()">Save Log</button>
            <button onclick="scrollToTop()">Scroll to Top</button>
            <button onclick="scrollToBottom()">Scroll to Bottom</button>
            <button onclick="toggleAutoScroll()" id="autoScrollBtn">Auto-scroll: ON</button>
        </div>
        <div class="command-controls">
            <input type="text" id="messageInput" class="command-input" placeholder="Type custom command">
            <button onclick="sendInputMessage()">Write Bytes</button>
            <select id="commandDropdown" class="command-dropdown">
                <option value="">-- Select Command --</option>
                <option value="S">Show System Status</option>
                <option value="F">Toggle Flash Buffer Enable/Disable</option>
                <option value="R">Factory Reset Flash Buffer</option>
                <option value="D">Disconnect WiFi (Simulate Offline)</option>
                <option value="C">Connect WiFi (Simulate Online)</option>
                <option value="H">Show Help</option>
                <option value="POST">Run Power-On Self Test</option>
                <option value="DEEP">Run Deep Sector Validation</option>
                <option value="STRESS">Run Stress Test (100 records)</option>
                <option value="RECOVERY">Run Power-Loss Recovery Test</option>
                <option value="ota-off">OTA Off</option>
                <option value="reboot">Reboot</option>
            </select>
            <button onclick="sendDropdownMessage()">Send</button>
        </div>
        <div id="otaBanner" class="ota-banner" style="display: none;">
            <strong>OTA MODE SHUTTING DOWN</strong><br>
            ESP-NOW communications will be restored momentarily...
        </div>
        <div id="console"></div>
        <div id="status" class="status disconnected">Disconnected</div>
    </div>

    <script>
        let ws;
        let autoScroll = true;
        let logBuffer = [];
        const console = document.getElementById('console');
        const status = document.getElementById('status');
        const autoScrollBtn = document.getElementById('autoScrollBtn');

        function connect() {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = protocol + '//' + window.location.hostname + ':' + window.location.port + '/webserialws';
            
            ws = new WebSocket(wsUrl);
            
            ws.onopen = function() {
                status.textContent = 'Connected';
                status.className = 'status connected';
                addToConsole('=== Connected to Lemon IO Serial Console ===\n');
            };
            
            ws.onmessage = function(event) {
                addToConsole(event.data);
            };
            
            ws.onclose = function() {
                status.textContent = 'Disconnected - Reconnecting...';
                status.className = 'status disconnected';
                setTimeout(connect, 2000);
            };
            
            ws.onerror = function() {
                status.textContent = 'Connection Error';
                status.className = 'status disconnected';
            };
        }

        function addToConsole(data) {
            const timestamp = new Date().toLocaleTimeString();
            const timestampedData = '[' + timestamp + '] ' + data;
            logBuffer.push(timestampedData);
            
            console.textContent += timestampedData;
            
            // Check for OTA shutdown sequence
            if (data.includes('OTA mode will be disabled') || data.includes('OTA mode disabled')) {
                showOtaBanner();
            }
            
            if (autoScroll) {
                console.scrollTop = console.scrollHeight;
            }
        }

        function showOtaBanner() {
            const banner = document.getElementById('otaBanner');
            banner.style.display = 'block';
            
            // Hide banner after 10 seconds
            setTimeout(() => {
                banner.style.display = 'none';
            }, 10000);
        }

        function clearConsole() {
            console.textContent = '';
            logBuffer = [];
        }

        function saveConsole() {
            const blob = new Blob([logBuffer.join('')], { type: 'text/plain' });
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'lemon-io-console-' + new Date().toISOString().replace(/[:.]/g, '-') + '.txt';
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            window.URL.revokeObjectURL(url);
        }

        function scrollToTop() {
            console.scrollTop = 0;
        }

        function scrollToBottom() {
            console.scrollTop = console.scrollHeight;
        }

        function toggleAutoScroll() {
            autoScroll = !autoScroll;
            autoScrollBtn.textContent = 'Auto-scroll: ' + (autoScroll ? 'ON' : 'OFF');
        }

        function sendInputMessage() {
            const messageInput = document.getElementById('messageInput');
            const message = messageInput.value.trim();
            
            if (message && ws && ws.readyState === WebSocket.OPEN) {
                ws.send(message);
                addToConsole('>>> Sent (input): ' + message + '\n');
                messageInput.value = '';
            } else if (!message) {
                alert('Please enter a message');
            } else {
                alert('WebSocket not connected');
            }
        }

        function sendDropdownMessage() {
            const dropdown = document.getElementById('commandDropdown');
            const command = dropdown.value;
            
            if (command && ws && ws.readyState === WebSocket.OPEN) {
                ws.send(command);
                addToConsole('>>> Sent (dropdown): ' + command + '\n');
                // Don't clear dropdown selection - keep it for repeated use
            } else if (!command) {
                alert('Please select a command from dropdown');
            } else {
                alert('WebSocket not connected');
            }
        }

        // Allow Enter key to send input message
        document.addEventListener('DOMContentLoaded', function() {
            const messageInput = document.getElementById('messageInput');
            messageInput.addEventListener('keypress', function(e) {
                if (e.key === 'Enter') {
                    sendInputMessage();
                }
            });
        });

        // Auto-connect on load
        connect();
    </script>
</body>
</html>)rawliteral";
