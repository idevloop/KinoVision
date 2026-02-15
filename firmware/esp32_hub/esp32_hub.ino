/*
 * KinoVision ESP32-S3 HUB - MQTT Version
 * SHAKE → Identify device via Memento (MQTT)
 * LEFT → Turn OFF
 * RIGHT → Turn ON
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// ---------- WIFI ----------
const char* WIFI_SSID = "********";
const char* WIFI_PASS = "********";

// ---------- ADAFRUIT IO MQTT ----------
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "********"  // ← UPDATE THIS
#define AIO_KEY         "********"       // ← UPDATE THIS

// ---------- RELAY PIN ----------
const int RELAY_PIN = 25;

// ---------- MQTT CLIENT ----------
WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// MQTT Topics (feeds)
Adafruit_MQTT_Publish   gesturePublish = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/gesture");

Adafruit_MQTT_Subscribe deviceSub = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/device");

// ---------- STATE ----------
String currentDevice = "";
bool relayState = false;
bool waitingForDevice = false;
unsigned long deviceRequestTime = 0;

// ---------- STRUCT ----------
typedef struct {
  char gesture[8];
} GesturePacket;

// ---------- DEBOUNCE ----------
unsigned long lastGestureTime = 0;
const unsigned long DEBOUNCE_MS = 500;

// ---------- RELAY CONTROL ----------
void setRelay(bool state) {
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);
  relayState = state;
  
  Serial.print("Relay: ");
  Serial.println(state ? "ON" : "OFF");
}

// ---------- MQTT CONNECT ----------
void MQTT_connect() {
  if (mqtt.connected()) return;

  Serial.print("Connecting to MQTT... ");
  
  uint8_t retries = 3;
  int8_t ret;
  
  while ((ret = mqtt.connect()) != 0) {
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Retrying in 2 seconds...");
    mqtt.disconnect();
    delay(2000);
    retries--;
    if (retries == 0) {
      Serial.println("MQTT connection failed!");
      return;
    }
  }
  
  Serial.println("MQTT connected!");
}

// ---------- ESP-NOW RX ----------
void onEspNowRecv(
  const esp_now_recv_info* info,
  const uint8_t* data,
  int len
) {
  if (len != sizeof(GesturePacket)) return;

  unsigned long now = millis();
  if (now - lastGestureTime < DEBOUNCE_MS) {
    Serial.println("Debounced");
    return;
  }
  lastGestureTime = now;

  GesturePacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  String gesture = String(pkt.gesture);
  gesture.trim();
  gesture.toLowerCase();

  Serial.println("\n─────────────────────────");
  Serial.print("Gesture: ");
  Serial.println(gesture.c_str());

  if (gesture == "shake") {
    // Send SHAKE to Memento via MQTT
    Serial.println("→ Publishing SHAKE to MQTT...");
    
    MQTT_connect();
    
    if (gesturePublish.publish("SHAKE")) {
      Serial.println("Published!");
      waitingForDevice = true;`
      deviceRequestTime = millis();
      Serial.println("Waiting for device identification...");
    } else {
      Serial.println("Publish failed!");
    }
    
  } else if (gesture == "left" || gesture == "right") {
    if (currentDevice != "") {
      Serial.print("→ Control device: ");
      Serial.println(currentDevice);
      
      if (gesture == "left") {
        setRelay(false);
        Serial.println("Relay turned OFF");
      } else {
        setRelay(true);
        Serial.println("Relay turned ON");
      }
      
    } else {
      Serial.println("No device detected yet");
      Serial.println("Do SHAKE gesture first!");
    }
    
    Serial.println("─────────────────────────\n");
  }
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);

  Serial.println("\n╔════════════════════════════╗");
  Serial.println("║ KINOVISION HUB (MQTT)      ║");
  Serial.println("╚════════════════════════════╝\n");

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi failed!");
    while (1) delay(1000);
  }

  Serial.println("\n✓ WiFi connected");
  Serial.print("  IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("  MAC: ");
  Serial.println(WiFi.macAddress());
  
  // Get WiFi channel
  uint8_t wifiChannel;
  wifi_second_chan_t secondChan;
  esp_wifi_get_channel(&wifiChannel, &secondChan);
  
  Serial.print("Channel: ");
  Serial.println(wifiChannel);
  Serial.println("\nSet wristband to channel " + String(wifiChannel) + "\n");

  // MQTT
  mqtt.subscribe(&deviceSub);
  MQTT_connect();

  // ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed");
    while (1);
  }

  esp_now_register_recv_cb(onEspNowRecv);
  Serial.println("✓ ESP-NOW ready");

  Serial.println("\n╔════════════════════════════╗");
  Serial.println("║         READY              ║");
  Serial.println("╠════════════════════════════╣");
  Serial.println("║ SHAKE → Identify device    ║");
  Serial.println("║ LEFT  → Turn OFF           ║");
  Serial.println("║ RIGHT → Turn ON            ║");
  Serial.println("╚════════════════════════════╝\n");
  
  Serial.print("Relay: GPIO");
  Serial.println(RELAY_PIN);
  Serial.print("MQTT Feeds:\n");
  Serial.print("  Publish: gesture\n");
  Serial.print("  Subscribe: device\n");
  Serial.println();
}

// ---------- LOOP ----------
void loop() {
  // Maintain MQTT connection
  MQTT_connect();
  
  // Process MQTT messages
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(100))) {
    if (subscription == &deviceSub) {
      String device = String((char*)deviceSub.lastread);
      device.trim();
      device.toLowerCase();
      
      Serial.print("MQTT received: ");
      Serial.println(device);
      
      if (device == "light" || device == "fan" || device == "ac") {
        currentDevice = device;
        Serial.print("Device identified: ");
        Serial.println(currentDevice);
        Serial.println("   Ready for LEFT/RIGHT control");
        waitingForDevice = false;
        
      } else if (device == "none") {
        Serial.println("No device detected in image");
        currentDevice = "";
        waitingForDevice = false;
      }
      
      Serial.println("─────────────────────────\n");
    }
  }
  
  // Timeout check
  if (waitingForDevice && (millis() - deviceRequestTime > 45000)) {
    Serial.println("Timeout waiting for device");
    waitingForDevice = false;
    Serial.println("─────────────────────────\n");
  }
  
  // Check WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost! Reconnecting...");
    WiFi.reconnect();
  }
  
  delay(10);
}
