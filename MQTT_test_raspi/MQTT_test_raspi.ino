#include "MultiWifi-SSID.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WebSocketsServer_Generic.h>
#include <ESPping.h>

const char* mqtt_server = "172.23.14.152";

WiFiClient espClient;
PubSubClient client(espClient);
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

unsigned long lastMsg = 0;
unsigned long lastPingTime = 0;
unsigned long lastReconnectAttempt = 0;
int mqttLatency = 0;

// Halaman HTML yang sudah dioptimasi untuk Mobile
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        * { box-sizing: border-box; }
        body { font-family: sans-serif; background: #4a4c4f; margin: 0; padding: 15px; display: flex; flex-direction: column; align-items: center; }
        .card { background: white; width: 100%; max-width: 400px; padding: 20px; border-radius: 12px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); margin-bottom: 15px; }
        h2 { color: #1a73e8; margin-top: 0; font-size: 1.4rem; }
        p { margin: 8px 0; color: #444; font-size: 0.95rem; }
        .data-box { background: #2c3e50; color: #2ecc71; padding: 12px; border-radius: 6px; overflow-x: auto; font-family: monospace; font-size: 0.85rem; }
        .status-on { color: #27ae60; font-weight: bold; }
        .status-off { color: #e74c3c; font-weight: bold; }
    </style>
</head>
<body>
    <div class="card">
        <h2>Test MQTT-Example Monitor</h2>
        <p><b>SSID:</b> <span id="ssid">...</span></p>
        <p><b>IP Address:</b> <span id="ipadd">...</span></p>
        <p><b>MQTT:</b> <span id="mqtt">...</span></p>
        <p><b>Latency:</b> <span id="lat">0</span> ms</p>
    </div>
    <div class="card">
        <p><b>Real-time JSON Data:</b></p>
        <div id="json" class="data-box">-</div>
    </div>

    <script>
        // Gunakan window.location.hostname agar otomatis deteksi IP/mDNS
        var gateway = `ws://${window.location.hostname}:81/`;
        var websocket;

        function initWebSocket() {
            websocket = new WebSocket(gateway);
            websocket.onmessage = function(event) {
                var data = JSON.parse(event.data);
                document.getElementById('ssid').innerHTML = data.ssid;
                document.getElementById('ipadd').innerHTML = data.ipadd;
                document.getElementById('mqtt').innerHTML = data.mqtt ? "<span class='status-on'>Connected</span>" : "<span class='status-off'>Disconnected</span>";
                document.getElementById('lat').innerHTML = data.lat;
                document.getElementById('json').innerHTML = JSON.stringify(data.sensor);
            };
        }
        window.onload = initWebSocket;
    </script>
</body>
</html>
)rawliteral";

void handleRoot() {
    server.send(200, "text/html", index_html);
}

// Fungsi Reconnect dengan ID Unik Berdasarkan MAC Address
boolean reconnect() {
    String clientId = "ESP32C3-";
    clientId += WiFi.macAddress();

    Serial.print("Mencoba koneksi MQTT dengan ID: ");
    Serial.println(clientId);

    if (client.connect(clientId.c_str())) {
        Serial.println("Terhubung ke MQTT");
    }
    return client.connected();
}

void setup() {
    Serial.begin(115200);
    connectWifi(true);
    client.setServer(mqtt_server, 1883);
    server.on("/", handleRoot);
    server.begin();
    webSocket.begin();
}

void loop() {
    bool wifiON = connectWifi();
    connectService("testMQTT");
    server.handleClient();
    webSocket.loop();

    unsigned long now = millis();

    // 1. Logika Reconnect MQTT Non-Blocking
    if (!client.connected()) {
        if (now - lastReconnectAttempt > 5000) {
            lastReconnectAttempt = now;
            if (reconnect()) {
                lastReconnectAttempt = 0;
            } else {
                Serial.println("Gagal ke MQTT, mencoba lagi 5 detik...");
            }
        }
    } else {
        client.loop();

        // 2. Update Latensi (Setiap 2 detik)
        if (now - lastPingTime > 2000) {
            lastPingTime = now;
            if(Ping.ping(mqtt_server, 1)) mqttLatency = Ping.averageTime();
            else mqttLatency = -1;
        }

        // 3. Pengolahan Data dan Publikasi (Setiap 2 detik)
        if (now - lastMsg > 2000) {
            lastMsg = now;

            float v1 = (analogRead(0) * 3.3) / 4095.0;
            float v2 = (analogRead(1) * 3.3) / 4095.0;

            // Gunakan satu JSON Document untuk efisiensi memori
            StaticJsonDocument<400> doc;
            
            // Buat objek sensor (untuk MQTT dan bagian dari WebSocket)
            JsonObject sensorData = doc.createNestedObject("sensor");
            sensorData["v1"] = v1;
            sensorData["v2"] = v2;

            // A. Kirim ke MQTT (Hanya data sensor saja)
            if (client.connected()) {
                char mqttBuffer[128];
                serializeJson(sensorData, mqttBuffer);
                client.publish("sensor/volt", mqttBuffer);
            }

            // B. Tambahkan Metadata Sistem untuk WebSocket
            doc["ssid"] = getConnectedSSID();
            doc["ipadd"] = WiFi.localIP().toString();
            doc["mqtt"] = client.connected();
            doc["lat"] = mqttLatency;

            // Broadcast JSON lengkap ke WebSocket
            String wsOutput;
            serializeJson(doc, wsOutput);
            webSocket.broadcastTXT(wsOutput);
            
            Serial.println("Sent: " + wsOutput);
        }
    }
}