#include <stdint.h>

#ifndef STATS_HTML_C
#define STATS_HTML_C

const char STATS_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
   <head>
       <title>LEMON STATS DASHBOARD</title>
       <meta name="viewport" content="width=device-width, initial-scale=1">
       <link rel="icon" type="image/png" href="favicon.png">
   <style>
       html {
           font-family: Arial, Helvetica, sans-serif;
           display: inline-block;
           text-align: center;
       }
       h1 {
           font-size: 1.8rem;
           color: white;
       }
       .topnav {
           overflow: hidden;
           background-color: #0A1128;
       }
       body {
           margin: 0;
       }
       .content {
           padding: 50px;
       }
       .card-grid {
           max-width: 800px;
           margin: 0 auto;
           display: grid;
           grid-gap: 2rem;
           grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
       }
       .card {
           background-color: white;
           box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);
       }
       .card-title {
           font-size: 1.2rem;
           font-weight: bold;
           color: #034078
       }
       .reading {
           font-size: 1.2rem;
           color: #1282A2;
       }
       .button {
           display: inline-block;
           padding: 10px 20px;
           margin: 10px;
           font-size: 18px;
           cursor: pointer;
           border: none;
           border-radius: 5px;
           transition: background-color 0.3s;
       }
       .button-active {
           background-color: #2980b9 !important;
           color: #fff;
       }
       .button-red {
           background-color: #e74c3c;
           color: #fff;
       }
       .button-orange {
           background-color: #e67e22;
           color: #fff;
       }
       .button-green {
           background-color: #2ecc71;
           color: #fff;
       }
       .button-blue {
           background-color: #3498db;
           color: #fff;
       }
   </style>
   </head>
   <body>
       <div class="topnav">
           <h1>LEMON STATISTICS</h1>
       </div>
       <div class="content">
           <button class="button button-green" id="updateButton">Update</button>
           <button class="button button-red" id="rebootButton">Reboot</button>
           <div class="card-grid">
               <div class="card">
                   <p class="card-title">Fixes</p>
                   <p class="reading"><span id="fixCount"></span></p>
               </div>
               <div class="card">
                   <p class="card-title">Good Uplinks</p>
                   <p class="reading"><span id="goodUplinkMessageCount"></span></p>
               </div>
               <div class="card">
                   <p class="card-title">Good MQTTs</p>
                   <p class="reading"><span id="privateMQTTUploadCount"></span></p>
               </div>
               <div class="card">
                   <p class="card-title">Bad Uplink %</p>
                   <p class="reading"><span id="uplinkBadMessagePercentage"></span> %</p>
               </div>
               <div class="card">
                   <p class="card-title">Bad Uplinks</p>
                   <p class="reading"><span id="badUplinkMessageCount"></span></p>
               </div>
               <div class="card">
                   <p class="card-title">Bad Length Uplinks</p>
                   <p class="reading"><span id="badLengthUplinkMsgCount"></span></p>
               </div>
               <div class="card">
                   <p class="card-title">Bad Checksum Uplinks</p>
                   <p class="reading"><span id="badChkSumUplinkMsgCount"></span></p>
               </div>
               <div class="card">
                   <p class="card-title">Missing Uplinks</p>
                   <p class="reading"><span id="uplinkMessageMissingCount"></span></p>
               </div>
               <div class="card">
                   <p class="card-title">Lemon Uptime (sec)</p>
                   <p class="reading"><span id="lemonUptime"></span></p>
               </div>               
                <div class="card">
                    <p class="card-title">Pipeline Draining</p>
                    <p class="reading"><span id="pipelineDraining"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Pipeline Length</p>
                    <p class="reading"><span id="pipelineLength"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Offline Throttle</p>
                    <p class="reading"><span id="offlineThrottleApplied"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Last MQTT Upload (sec)</p>
                    <p class="reading"><span id="last_private_mqtt_upload_at"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Last Head Commit (sec)</p>
                    <p class="reading"><span id="last_head_committed_at"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Last Internet Check (sec)</p>
                    <p class="reading"><span id="lastCheckForInternetConnectivityAt"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Mako Sensor Read Throttle (ms) </p>
                    <p class="reading"><span id="min_sens_read"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Mako Throttled Sensor Read (ms)</p>
                    <p class="reading"><span id="sens_read"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Mako Max Throttled Sensor Read (ms)</p>
                    <p class="reading"><span id="max_sens_read"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Mako Actual Sensor Read (ms)</p>
                    <p class="reading"><span id="act_sens_read"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Mako Max Actual Sensor Read (ms)</p>
                    <p class="reading"><span id="max_act_sens_read"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Mako Pause Before Reply (ms)</p>
                    <p class="reading"><span id="quiet_b4_uplink"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Lemon Free Heap (bytes)</p>
                    <p class="reading"><span id="free_heap_bytes"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Lemon Largest Free Block (bytes)</p>
                    <p class="reading"><span id="largest_free_block"></span></p>
                </div>
                <div class="card">
                    <p class="card-title">Lemon Minimum Free Ever (bytes)</p>
                    <p class="reading"><span id="minimum_free_ever"></span></p>
                </div>
            </div>
       </div>

       <iframe src=\"/webserial\" width=\"1000\" height=\"1000\" frameborder=\"0\"></iframe>

       <script>

       var gateway = `ws://${window.location.hostname}/ws`;
       var websocket;
       // Init web socket when the page loads
       window.addEventListener('load', onload);

       function onload(event) {
           initWebSocket();
       }

       function getReadings(){
           websocket.send("getReadings");
       }

       function initWebSocket() {
           console.log('Trying to open a WebSocket connection…');
           websocket = new WebSocket(gateway);
           websocket.onopen = onOpen;
           websocket.onclose = onClose;
           websocket.onmessage = onMessage;
       }

       // When websocket is established, call the getReadings() function
       function onOpen(event) {
           console.log('Connection opened');
           getReadings();
       }

       function onClose(event) {
           console.log('Connection closed');
           setTimeout(initWebSocket, 2000);
       }

       // Function that receives the message from the ESP32 with the readings
       function onMessage(event) {
           console.log(event.data);
           var myObj = JSON.parse(event.data);
           var keys = Object.keys(myObj);

           for (var i = 0; i < keys.length; i++){
               var key = keys[i];
               document.getElementById(key).innerHTML = myObj[key];
           }
       }


   // Function to handle button activation
   function activateButton(buttonId) {
       var button = document.getElementById(buttonId);
       button.classList.add("button-active");
       setTimeout(function() {
           button.classList.remove("button-active");
       }, 100);
   }



   // Function to send a POST request
   function sendPostRequest(url, buttonId) {
       activateButton(buttonId);
       fetch(url, {
           method: 'POST',
           headers: {
               'Content-Type': 'application/x-www-form-urlencoded'
           },
           body: 'button=' + encodeURIComponent(buttonId)
       });
   }
   // Function to handle button clicks
   function handleButtonClick(buttonId) {
       sendPostRequest(window.location.href, buttonId);
   }

   // Function to handle update button click
   function handleUpdateButtonClick() {
       window.location.href = "/update";
   }

   // Attach event listener to the update button
   document.getElementById("updateButton").addEventListener("click", handleUpdateButtonClick);

   document.getElementById("rebootButton").addEventListener("click", function() {
       handleButtonClick("rebootButton");
   });


   
   // Function to send a GET request
   function sendGetRequest(url) {
       fetch(url, {
           method: 'GET',
           headers: {
               'Content-Type': 'text/html'
           }
       })
       .then(response => response.text())
       .then(data => document.documentElement.innerHTML = data)
       .catch(error => console.error('Error:', error));
   }
   
  function handleKeyDown(event) {
       switch(event.key) {
           case 'u':
               handleUpdateButtonClick();
               break;
       }
   }

   // Attach keydown event listener to document
       document.addEventListener("keydown", handleKeyDown);

   </script>
   </body>
</html>)rawliteral";

const uint32_t STATS_HTML_SIZE = sizeof(STATS_HTML);

#endif
