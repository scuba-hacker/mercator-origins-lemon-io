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
       /* Styling for the drop-down */
       select {
           padding: 10px;
           font-size: 18px;
           border-radius: 5px;
           margin: 10px;
           border: 1px solid #ccc;
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
           <button class="button button-blue" id="bluepadMapButton">Bluepad Map</button>
           <button class="button button-blue" id="mapButton">Local Map</button>
           <button class="button button-green" id="updateButton">Update</button>
           <button class="button button-red" id="rebootButton">Reboot</button>

            <br><br>

          <select id="sortedWaypointsDropdown">
               <!-- Options will be populated dynamically -->
           </select>

            <button class="button button-green" id="showOnMapButton">Override Location</button>

            <br><br>

          <select id="sortedWaypointsDropdown2">
               <!-- Options will be populated dynamically -->
           </select>

            <button class="button button-blue" id="setTargetButton">Set Target Feature</button>

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

       <iframe src="/webserial" width="1000" height="1000" frameborder="0"></iframe>

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
    if (buttonId == "showOnMapButton")
        sendSelectionPostRequest(window.location.href, buttonId);
    else
        if (buttonId == "setTargetButton")
            sendSetTargetSelectionPostRequest(window.location.href, buttonId);
        else
            sendPostRequest(window.location.href, buttonId);
   }

   function sendSelectionPostRequest(url, buttonId) {
    activateButton(buttonId);

    // Get the selected feature to show from the drop-down
    var dropdown = document.getElementById("sortedWaypointsDropdown");
    var selectedFeature = dropdown.options[dropdown.selectedIndex].value;

    // Send the POST request with both button and feature choice
    fetch(url, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded'
        },
        body: 'button=' + encodeURIComponent(buttonId) + '&choice=' + encodeURIComponent(selectedFeature)
    });
   }

   function sendSetTargetSelectionPostRequest(url, buttonId) {
    activateButton(buttonId);

    // Get the selected target from the drop-down
    var dropdown = document.getElementById("sortedWaypointsDropdown2");
    var selectedTarget = dropdown.options[dropdown.selectedIndex].value;

    // Send the POST request with both button and target
    fetch(url, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded'
        },
        body: 'button=' + encodeURIComponent(buttonId) + '&target=' + encodeURIComponent(selectedTarget)
    });
   }

   // Function to handle update button click
   function handleUpdateButtonClick() {
       window.location.href = "/update";
   }

   // Function to handle update button click
   function handleMapButtonClick() {
       window.location.href = "/map";
   }

   // Function to handle update button click
   function handleBluepadMapButtonClick() {
       window.location.href = "https://www.bluepad.co.uk";
   }

   // Attach event listener to the update button
   document.getElementById("mapButton").addEventListener("click", handleMapButtonClick);

   // Attach event listener to the update button
   document.getElementById("bluepadMapButton").addEventListener("click", handleBluepadMapButtonClick);

   // Attach event listener to the update button
   document.getElementById("updateButton").addEventListener("click", handleUpdateButtonClick);

   document.getElementById("rebootButton").addEventListener("click", function() {
       handleButtonClick("rebootButton");
   });

   document.getElementById("showOnMapButton").addEventListener("click", function() {
        handleButtonClick("showOnMapButton");
    });

    document.getElementById("setTargetButton").addEventListener("click", function() {
        handleButtonClick("setTargetButton");
    });

   // Array of strings to populate the drop-down
   // from navigation array copy/paste into temp.txt

   var waypoints = [   
   ' < No Waypoint Shown >',
    'Canoe',
    'The Sub',
    'Scimitar Car 5.5m',
    'Spitfire Car 6m',
    'Lightning Boat 5.5m',
    'Caves Centre',
    'Lion Entrance @ Caves',
    'Red Isis Bike @ Caves',
    'Blue Raleigh Bike @ Caves',
    'Cave PC Laptop',
    'Cargo 2.5m',
    'The Hole 18m',
    'Dance Platform 6m',
    'Bus 2m',
    'Confined Area',
    'Commer Van 6m',
    'White Boat 7m',
    'Cargo 8m',
    'Cargo Rusty 8m',
    'Portacabin 8m',
    'Shallow Platform 2m',
    'Milk Float 6.5m',
    'Chicken Hutch Boat 6.5m',
    'Skittles Sweet Bowl 5.5m',
    'Sticky Up Boat 5m',
    'Lady of Kent Search Light 5m',
    'Traffic Lights 7m',
    'Half Die Hard Taxi 8m',
    'Boat In A Hole 7m',
    'Iron Fish 2m',
    'Wreck Site 6m',
    'Dive/Spike Boat 7m',
    'White Day boat by platform 6m',
    'Port Holes Boat 4.5m',
    'Dragon Boat 7.5m',
    'Dive Bell 4m',
    'Lifeboat 6.5m',
    'London Black Cab 7m',
    'RIB Boat 6m',
    'Tin/Cabin Boat 7m',
    'Thorpe Orange Boat 5.5m',
    'VW Camper Van and Seahorse 5.5m',
    'Listing Sharon 7.5m',
    'Plane 6m',
    'Holey Ship 4.5m',
    'Claymore 6.5m',
    'Swim Through - no crates 6m',
    'Swim Through - mid 6m',
    'Swim Through - crates 6m',
    'Orca Van 5.5m',
    'Dinghy Boat',
    'Quarry Machine in Reeds',
    'Metal Grated Box',
    '4 crates in a line',
    'Lone crate',
    'Collapsed Metal',
    'Boat with Chain Links',
    'Pot in a box',
    'Seahorse Mid-Water',
    'Headless Nick',
    'Headless Tom Reeds',
    'Cement Mixer',
    'Tyre',
    'Roadworks Sign',
    'Fireworks Launcher',
    '2 Buried Boats in Reeds',
    'Half Buried Solo Boat',
    'Half Buried Bike',
    'Desk with Keyboard',
    'La Mouette Boat',
    'Memorial Stone - Kit 7.5m',
    'Fruit Machine 5.5m',
    'Lone Crate 7m',
    '? near plane 6m',
    'Cotton Reel 3m',
    'Dumpy Cylinder 6m',
    'Disused Pontoon',
    'Cafe Jetty',
    'Mid Jetty',
    'Old Slipway'
    ];

   // Sort the array alphabetically
   waypoints.sort();

     // Get the drop-down element
   var dropdown1 = document.getElementById("sortedWaypointsDropdown");
   var dropdown2 = document.getElementById("sortedWaypointsDropdown2");

   // Function to dynamically populate the drop-down
   function populateDropdown(optionsArray) {
       // Loop through the array
       optionsArray.forEach(function(item) {

           var option = document.createElement("option");
           option.text = item;
           dropdown1.add(option);

           var option2 = document.createElement("option");
           option2.text = item;
           dropdown2.add(option2);
       });
   }

   // Populate the drop-down on page load
   populateDropdown(waypoints);
   
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
           case 'm':
               handleMapButtonClick();
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
