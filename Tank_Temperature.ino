/*
 * This is for my boat hot water heater.
 * You can change the text in the webpage to your language (Spanish here for my GF)
 * 
 * The heater will go to the set temperature (Make sure the sensor has a good contact with the tank) and keep it until timeout.
 *
 * uncomment heaterOn = false; if you want it to switch OFF and stay OFF after it reached temperture
 *
 */

#include <ESP8266WiFi.h> // 1st make sure you're not on esp32 board
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Network credentials
const char* ssid = "Sailrover2G";
const char* password = "robitaille";

// Pin definitions
const int switch1 = 2;  // D4 NodeMCU Active LOW (changed from D5) This is the n/o relay
const int oneWireBus = 4;  // D2 for DS18B20 add a 4.7k resistor from 3.3v to this pin

// DS18B20 setup
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

// Static IP configuration adjust to your network
IPAddress local_IP(192, 168, 1, 184); 
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// AsyncWebServer and WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Variables
unsigned long startMillis = 0;
const unsigned long TIMEOUT = 30 * 60 * 1000; // 30 minutes timeout to save energy can be whatever you want
String tankStatus = "OFF";
bool heaterOn = false;
float currentTemp = 0.0;
String setpointTemp = "38"; // Default 38°C seems to me warm enough (Adjust the min and max below to your needs)(min="20" max="39")
const char* PARAM_INPUT = "value";
const char* PARAM_ACTION = "action";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Calentador Control</title>
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    h2 {font-size: 2.3rem;}
    p {font-size: 1.9rem;}
    body {max-width: 400px; margin:0px auto; padding-bottom: 25px;}
    .slider { -webkit-appearance: none; margin: 14px; width: 300px; height: 25px; background: #FFD65C;
      outline: none; -webkit-transition: .2s; transition: opacity .2s;}
    .slider::-webkit-slider-thumb {-webkit-appearance: none; appearance: none; width: 35px; height: 35px; background: #003249; cursor: pointer;}
    .slider::-moz-range-thumb { width: 35px; height: 35px; background: #003249; cursor: pointer;}
    .button {padding: 15px 25px; font-size: 24px; cursor: pointer; text-align: center; color: white; border: none; border-radius: 15px;}
    .button-on {background-color: #4CAF50;}
    .button-off {background-color: #ff0000;}
  </style>
</head>
<body>
  <h2>Calientador</h2>
  <p>Temp. Presente: <span id="currentTemp">0</span> C</p>
  <p>Temp. Quiero: <span id="textSliderValue">%SETPOINTTEMP%</span> C</p>
  <p><input type="range" onchange="updateSlider(this)" id="tempSlider" min="28" max="39" value="%SETPOINTTEMP%" step="1" class="slider"></p>
  <p>Status: <span id="tankStatus">%TANKSTATUS%</span></p>
  <p>Queda Tiempo: <span id="timeRemaining">--:--</span></p>
  <p><button id="controlButton" class="button button-off" onclick="toggleHeater()">Start</button></p>

<script>
  var ws = new WebSocket('ws://' + window.location.hostname + '/ws');
  ws.onmessage = function(event) {
    var data = JSON.parse(event.data);
    document.getElementById('currentTemp').innerHTML = data.temp.toFixed(1);
    document.getElementById('tankStatus').innerHTML = data.status;
    document.getElementById('timeRemaining').innerHTML = data.timer;
    var button = document.getElementById('controlButton');
    if (data.status === 'CALENTANDO') {
      button.className = 'button button-on';
      button.innerHTML = 'Stop';
    } else {
      button.className = 'button button-off';
      button.innerHTML = 'Start';
    }
  };

  function updateSlider(element) {
    var sliderValue = document.getElementById('tempSlider').value;
    document.getElementById('textSliderValue').innerHTML = sliderValue;
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/slider?value=" + sliderValue, true);
    xhr.send();
  }

  function toggleHeater() {
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/toggle?action=" + (document.getElementById('controlButton').innerHTML === 'Start' ? 'start' : 'stop'), true);
    xhr.send();
  }
</script>
</body>
</html>
)rawliteral";

String processor(const String& var) {
  if (var == "SETPOINTTEMP") {
    return setpointTemp;
  }
  if (var == "TANKSTATUS") {
    return tankStatus;
  }
  return String();
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("WebSocket client connected");
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("WebSocket client disconnected");
  }
}
void reconnect(){
  // Connect to Wi-Fi
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }
  WiFi.begin(ssid, password);
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(switch1, LOW);
    heaterOn = false; // Ensure heater stays off
    tankStatus = "APAGADOS";
    delay(5000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println(WiFi.localIP());
  
}

void setup() {
  Serial.begin(115200);
  pinMode(switch1, OUTPUT);
  digitalWrite(switch1, LOW); // Heater OFF (Active LOW)
  sensors.begin();

  // Connect to Wi-Fi
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println(WiFi.localIP());

  // WebSocket setup
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Web server routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/slider", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam(PARAM_INPUT)) {
      setpointTemp = request->getParam(PARAM_INPUT)->value();
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam(PARAM_ACTION)) {
      String action = request->getParam(PARAM_ACTION)->value();
      if (action == "start") {
        heaterOn = true;
        startMillis = millis();
      } else {
        heaterOn = false;
        digitalWrite(switch1, LOW); // Heater OFF
      }
    }
    request->send(200, "text/plain", "OK");
  });

  server.begin();
}

void loop() {
  ws.cleanupClients();

  // Read temperature
  sensors.requestTemperatures();
  currentTemp = sensors.getTempCByIndex(0);

  // Heater control logic
  if (heaterOn) {
    unsigned long currentMillis = millis();
    
    // Safety timeout
    if (currentMillis - startMillis >= TIMEOUT) {
      heaterOn = false;
      digitalWrite(switch1, LOW); // Heater OFF
      tankStatus = "APAGADOS";
    }
    // Temperature control
    else if (currentTemp >= setpointTemp.toFloat()) {
      digitalWrite(switch1, LOW); // Heater OFF
      tankStatus = "APAGADOS";
     // heaterOn = false; // Ensure heater stays off  //uncomment to save energy and not use the total of the TIMEOUT
    } else {
      digitalWrite(switch1, HIGH); // Heater ON
      tankStatus = "CALENTANDO";
    }
  } else {
    digitalWrite(switch1, LOW); // Heater OFF
    tankStatus = "APAGADOS";
  }

  // Send temperature, status, and timer to clients
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 1000) {
    // Calculate remaining time
    String timerStr = "--:--";
    if (heaterOn) {
      unsigned long elapsed = millis() - startMillis;
      if (elapsed < TIMEOUT) {
        unsigned long remaining = TIMEOUT - elapsed;
        unsigned long minutes = remaining / 60000;
        unsigned long seconds = (remaining % 60000) / 1000;
        timerStr = String(minutes) + ":" + (seconds < 10 ? "0" : "") + String(seconds);
      } else {
        timerStr = "0:00";
      }
    }

    String json = "{\"temp\":" + String(currentTemp) + ",\"status\":\"" + tankStatus + "\",\"timer\":\"" + timerStr + "\"}";
    ws.textAll(json);
    lastUpdate = millis();
    
    // Serial debug
    Serial.print("Temp: ");
    Serial.print(currentTemp);
    Serial.print("°C, Setpoint: ");
    Serial.print(setpointTemp);
    Serial.print("°C, Status: ");
    Serial.print(tankStatus);
    Serial.print(", Timer: ");
    Serial.println(timerStr);
  }
  if (WiFi.status() != WL_CONNECTED) {
    reconnect();
  }
}
