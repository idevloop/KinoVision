/*
 * KinoVision Wristband - EASY GESTURES
 * XIAO ESP32-S3 + MPU6050
 * ESP-NOW sender (relaxed detection)
 */

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ---------- PINS ----------
#define SDA_PIN D4
#define SCL_PIN D5
#define LED_PIN LED_BUILTIN

// ---------- EASY GESTURE CONFIG ----------
#define SHAKE_THRESHOLD     18.0    
#define TILT_THRESHOLD      6.0    // left/right
#define MIN_MOVEMENT        10.0   // Minimum movement to detect anything
#define GESTURE_COOLDOWN    1200   // Faster response

// ---------- HUB DETAILS ----------
uint8_t HUB_MAC[] = { 0xEC, 0x64, 0xC9, 0x61, 0xC5, 0xCC };

// SET THIS TO HUB CHANNEL (printed by hub)
#define HUB_CHANNEL 10

// ---------- STRUCT ----------
typedef struct {
  char gesture[8];
} GesturePacket;

GesturePacket packet;

// ---------- STATE ----------
Adafruit_MPU6050 mpu;
unsigned long lastGestureTime = 0;
float baseX = 0, baseY = 0, baseZ = 0;

// For debugging
bool debugMode = true;

// ---------- CALLBACK ----------
void onSent(const uint8_t* mac, esp_now_send_status_t status) {
  Serial.print("ESP-NOW: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✓ SENT" : "✗ FAIL");
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.println("\n╔════════════════════════════╗");
  Serial.println("║  WRISTBAND - EASY MODE     ║");
  Serial.println("╚════════════════════════════╝");

  // --- WiFi INIT (CRITICAL) ---
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  delay(500);

  Serial.print("Wrist MAC: ");
  Serial.println(WiFi.macAddress());

  // Lock channel to hub
  esp_wifi_set_channel(HUB_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // --- MPU6050 ---
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!mpu.begin()) {
    Serial.println("MPU6050 NOT FOUND");
    while (1);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("Calibrating... Keep wrist still!");
  calibrate();
  Serial.println("✓ Calibration done");

  // --- ESP-NOW ---
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW INIT FAILED");
    while (1);
  }

  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, HUB_MAC, 6);
  peer.channel = HUB_CHANNEL;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("PEER ADD FAILED");
    while (1);
  }

  digitalWrite(LED_PIN, LOW);

  Serial.println("\n╔════════════════════════════╗");
  Serial.println("║         READY              ║");
  Serial.println("╠════════════════════════════╣");
  Serial.println("║ SHAKE → Quick shake        ║");
  Serial.println("║ LEFT  → Tilt left          ║");
  Serial.println("║ RIGHT → Tilt right         ║");
  Serial.println("╚════════════════════════════╝\n");
}

// ---------- LOOP ----------
void loop() {
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  // Get acceleration relative to baseline
  float ax = a.acceleration.x - baseX;
  float ay = a.acceleration.y - baseY;
  float az = a.acceleration.z - baseZ;

  // Total acceleration magnitude
  float total = sqrt(ax*ax + ay*ay + az*az);

  // Cooldown check
  if (millis() - lastGestureTime < GESTURE_COOLDOWN) return;
  
  // Minimum movement check
  if (total < MIN_MOVEMENT) return;

  // Classify gesture
  String gesture = classifyGesture(ax, ay, az, total);
  if (gesture == "IDLE") return;

  // Debug output
  if (debugMode) {
    Serial.print("[X:");
    Serial.print(ax, 1);
    Serial.print(" Y:");
    Serial.print(ay, 1);
    Serial.print(" Z:");
    Serial.print(az, 1);
    Serial.print(" Total:");
    Serial.print(total, 1);
    Serial.println("]");
  }

  // Send gesture
  lastGestureTime = millis();
  gesture.toCharArray(packet.gesture, sizeof(packet.gesture));

  Serial.print(">>> ");
  Serial.println(gesture);

  digitalWrite(LED_PIN, HIGH);
  esp_now_send(HUB_MAC, (uint8_t*)&packet, sizeof(packet));
  delay(80);
  digitalWrite(LED_PIN, LOW);
}

// ---------- HELPERS ----------
void calibrate() {
  baseX = 0;
  baseY = 0;
  baseZ = 0;
  
  for (int i = 0; i < 100; i++) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    baseX += a.acceleration.x;
    baseY += a.acceleration.y;
    baseZ += a.acceleration.z;
    delay(20);
  }
  
  baseX /= 100;
  baseY /= 100;
  baseZ /= 100;
  
  Serial.print("Baseline: X=");
  Serial.print(baseX, 2);
  Serial.print(" Y=");
  Serial.print(baseY, 2);
  Serial.print(" Z=");
  Serial.println(baseZ, 2);
}

String classifyGesture(float x, float y, float z, float total) {
  // SHAKE: Any rapid movement
  if (total > SHAKE_THRESHOLD) {
    return "SHAKE";
  }
  
  // LEFT: Tilt wrist to the left (negative Y)
  // More relaxed - just needs significant Y movement
  if (y < -TILT_THRESHOLD && total > MIN_MOVEMENT) {
    return "LEFT";
  }
  
  // RIGHT: Tilt wrist to the right (positive Y)
  if (y > TILT_THRESHOLD && total > MIN_MOVEMENT) {
    return "RIGHT";
  }
  
  return "IDLE";
}