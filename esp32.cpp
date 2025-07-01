#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <Preferences.h>

Preferences preferences;

const char* ap_ssid = "caidat";
const char* ap_password = "";
WebServer server(80);

String htmlForm = R"rawliteral(
<!DOCTYPE html>
<html>
  <head><meta charset="UTF-8"></head>
  <body>
    <h2>Đăng ký thiết bị</h2>
    <form action="/submit" method="POST">
      Username: <input type="text" name="username"><br>
      Tên thiết bị: <input type="text" name="name"><br>
      SSID WiFi: <input type="text" name="ssid"><br>
      Password WiFi: <input type="text" name="password"><br>
      Mã xác thực (Auth Code): <input type="text" name="authcode"><br>
      <input type="submit" value="Gửi">
    </form>
  </body>
</html>
)rawliteral";

bool shouldRegister = false;
String inputSSID = "";
String inputPASS = "";
String authCode = "";
String username = "";
String nameiot ="";

void handleRoot() {
  server.send(200, "text/html", htmlForm);
}

void handleSubmit() {
  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("authcode") && server.hasArg("name") && server.hasArg("username")) {
    inputSSID = server.arg("ssid");
    inputPASS = server.arg("password");
    authCode = server.arg("authcode");
    nameiot = server.arg("name");
    username = server.arg("username");

    preferences.begin("wifi", false);
    preferences.putString("ssid", inputSSID);
    preferences.putString("pass", inputPASS);
    preferences.putString("auth", authCode);
    preferences.end();

    server.send(200, "text/plain", "Đã nhận thông tin. Thiết bị sẽ kết nối và đăng ký.");
    shouldRegister = true;
  } else {
    server.send(400, "text/plain", "Thiếu thông tin. Vui lòng điền đầy đủ.");
  }
}

void startRegistrationAP() {
  WiFi.softAP(ap_ssid, ap_password);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.begin();

  while (!shouldRegister) {
    server.handleClient();
    delay(10);
  }

  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.begin(inputSSID.c_str(), inputPASS.c_str());

  int timeout = 15000;
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    const char* server_ip = "10.42.0.1";
    const int server_port = 8080;

    if (client.connect(server_ip, server_port)) {
      String mac = WiFi.macAddress();
      StaticJsonDocument<256> doc;
      doc["macId"] = mac;
      doc["username"] = username;
      doc["name"] = nameiot;
      doc["water"] = 1;
      doc["do_am"] = 10;
      String body;
      serializeJson(doc, body);

      String path = "/auth/dangkythietbi?authCode=" + authCode;

      client.print(F("POST ")); client.print(path); client.print(F(" HTTP/1.1\r\n"));
      client.print(F("Host: ")); client.print(server_ip); client.print(F(":")); client.print(server_port); client.print(F("\r\n"));
      client.println(F("Content-Type: application/json"));
      client.print(F("Content-Length: ")); client.println(body.length());
      client.println(F("Connection: close"));
      client.println();
      client.print(body);
      client.println();
      client.flush();
      client.stop();
    }
  }

  delay(1000);
  ESP.restart();
}

String Mac;
const char* websocket_server_host = "10.42.0.1";
const uint16_t websocket_server_port = 8080;
const char* websocket_server_path = "/ws/device";

WebSocketsClient webSocket;

const int doamP = 34;
const int congtacP = 26;
const int ledP = 2;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_CONNECTED:
      sendDoam();
      break;
    case WStype_TEXT: {
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, payload, length);
      if (error) return;

      const char* command = doc["command"];
      if (command) {
        if (strcmp(command, "ON") == 0) {
          digitalWrite(congtacP, HIGH);
          digitalWrite(ledP, HIGH);
          sendDoam();
        } else if (strcmp(command, "OFF") == 0) {
          digitalWrite(congtacP, LOW);
          digitalWrite(ledP, LOW);
        } else if (strcmp(command, "RESET") == 0) {
          const char* token = doc["token"];
          if (token) {
            if(sendReset(token)){
              resetiot();
            }
          }
        }
      }
      break;
    }
    default:
      break;
  }
}

bool sendReset(const String& jwttoken) {
  WiFiClient client;
  const char* reset_server_ip = "10.42.0.1";
  const int reset_server_port = 8080;

  if (!client.connect(reset_server_ip, reset_server_port)) {
    return false;
  }

  String id = WiFi.macAddress();
  String path = "/iot/" + id;

  StaticJsonDocument<100> doc;
  doc["command"] = "ok";
  String payload;
  serializeJson(doc, payload);

  client.print(F("POST ")); client.print(path); client.print(F(" HTTP/1.1\r\n"));
  client.print(F("Host: ")); client.print(reset_server_ip); client.print(F(":")); client.print(reset_server_port); client.print(F("\r\n"));
  client.print(F("Authorization: Bearer ")); client.print(jwttoken); client.print(F("\r\n"));
  client.println(F("Content-Type: application/json"));
  client.print(F("Content-Length: ")); client.println(payload.length());
  client.println(F("Connection: close"));
  client.println();
  client.print(payload);
  client.println();
  client.flush();
  client.stop();
  return true;
}

void resetiot(){
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();
  delay(1000);
  ESP.restart();
}

void sendDoam() {
  if (WiFi.status() != WL_CONNECTED || !webSocket.isConnected()) return;

  int analogValue = analogRead(doamP);
  float doampt = map(analogValue, 4095, 0, 0, 100);
  if (doampt < 0) doampt = 0;
  if (doampt > 100) doampt = 100;

  StaticJsonDocument<128> doc;
  doc["Mac"] = Mac;
  doc["Doam"] = doampt;

  char buffer[128];
  size_t n = serializeJson(doc, buffer);
  webSocket.sendTXT(buffer, n);
}

void setup() {
  pinMode(ledP, OUTPUT);
  pinMode(congtacP, OUTPUT);
  pinMode(doamP, INPUT);
  digitalWrite(congtacP, LOW);
  digitalWrite(ledP, LOW);

  preferences.begin("wifi", true);
  String savedSSID = preferences.getString("ssid", "");
  String savedPASS = preferences.getString("pass", "");
  preferences.end();

  if (savedSSID != "") {
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());

    int timeout = 15000;
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
      delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Mac = WiFi.macAddress();
      String ws_path_full = String(websocket_server_path) + "?macId=" + Mac;
      webSocket.begin(websocket_server_host, websocket_server_port, ws_path_full.c_str());
      webSocket.onEvent(webSocketEvent);
      webSocket.setReconnectInterval(5000);
    }
  } else {
    startRegistrationAP();
  }
}

void loop() {
  webSocket.loop();
}
