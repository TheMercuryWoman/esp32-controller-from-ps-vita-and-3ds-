#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "EmirWifi";
const char* password = "EmirGulserin2007";

WebServer server(80);

String lastButtons = "0";
int lastLX = 128, lastLY = 128, lastRX = 128, lastRY = 128;
String lastDevice = "none yet";

void handleController() {
  if (server.hasArg("btn")) lastButtons = server.arg("btn");
  if (server.hasArg("lx"))  lastLX = server.arg("lx").toInt();
  if (server.hasArg("ly"))  lastLY = server.arg("ly").toInt();
  if (server.hasArg("rx"))  lastRX = server.arg("rx").toInt();
  if (server.hasArg("ry"))  lastRY = server.arg("ry").toInt();
  if (server.hasArg("dev")) lastDevice = server.arg("dev");

  Serial.printf("[%s] BTN:%s  LX:%d LY:%d  RX:%d RY:%d\n",
    lastDevice.c_str(), lastButtons.c_str(), lastLX, lastLY, lastRX, lastRY);

  server.send(200, "text/plain", "OK");
}

void handleState() {
  String json = "{\"dev\":\"" + lastDevice + "\",";
  json += "\"btn\":\"" + lastButtons + "\",";
  json += "\"lx\":" + String(lastLX) + ",";
  json += "\"ly\":" + String(lastLY) + ",";
  json += "\"rx\":" + String(lastRX) + ",";
  json += "\"ry\":" + String(lastRY) + "}";
  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
<title>Controller State</title>
<style>
  body { font-family: monospace; background: #111; color: #eee; padding: 20px; }
  .row { font-size: 20px; margin: 8px 0; }
  .label { color: #888; display: inline-block; width: 140px; }
  #dev { font-weight: bold; color: #6cf; }
</style>
</head>
<body>
  <h2>Controller State</h2>
  <div class="row"><span class="label">Device:</span><span id="dev">-</span></div>
  <div class="row"><span class="label">Buttons:</span><span id="btn">-</span></div>
  <div class="row"><span class="label">Left Stick:</span><span id="left">-</span></div>
  <div class="row"><span class="label">Right Stick:</span><span id="right">-</span></div>

  <script>
    async function poll() {
      try {
        const res = await fetch('/state');
        const d = await res.json();
        document.getElementById('dev').textContent = d.dev;
        document.getElementById('btn').textContent = d.btn;
        document.getElementById('left').textContent = 'X=' + d.lx + ' Y=' + d.ly;
        document.getElementById('right').textContent = 'X=' + d.rx + ' Y=' + d.ry;
      } catch (e) {}
      setTimeout(poll, 100);
    }
    poll();
  </script>
</body>
</html>
)HTML";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/controller", handleController);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}