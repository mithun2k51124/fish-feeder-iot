#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <time.h>

// ============================================================================
// ESP32 WiFi Fish Feeder Firmware (Free Public Cloud Broker Edition)
// ============================================================================

// ----------------------------------------------------------------------------
// 1. CONFIGURATION
// ----------------------------------------------------------------------------
// Enter your home or phone hotspot Wi-Fi credentials:
const char* WIFI_SSID     = "Mithun2k5";
const char* WIFI_PASS     = "mithun2k5";

// Free Public HiveMQ Broker (No account or credit card required!)
const char* MQTT_HOST     = "broker.hivemq.com";
const int   MQTT_PORT     = 1883; // Standard reliable TCP port

// Unique team topic prefix (prevents interference from others)
const char* TOPIC_CMD     = "mithun_feeder/fishfeeder/cmd";
const char* TOPIC_STATUS  = "mithun_feeder/fishfeeder/status";

// Timezone settings (UTC +5:30 for India = 19800 seconds)
const long  GMT_OFFSET_SEC = 19800;
const int   DST_OFFSET_SEC = 0;

// Daily automatic feeding schedule (24-hour format: 8:00 AM and 6:00 PM)
const int   FEED_HOURS[]   = {8, 18};
const int   NUM_FEEDS      = sizeof(FEED_HOURS) / sizeof(FEED_HOURS[0]);

// Hardware Pinout (1 Servo for Dispenser Gate)
const int   SERVO_PIN      = 18; // Feeder dispenser gate servo pin

// Servo calibration angles
const int   CLOSED_ANGLE   = 0;  // Gate closed position
const int   OPEN_ANGLE     = 90; // Gate open position (drop food)

// Safety & portion tuning
const unsigned long COOLDOWN_MS = 5000; // 5-second minimum between feeds

// ----------------------------------------------------------------------------
// 2. GLOBAL OBJECTS & STATE
// ----------------------------------------------------------------------------
Servo feederServo;
WiFiClient espClient;
PubSubClient mqtt(espClient);

volatile bool feedRequested = false;
unsigned long lastFeedMs = 0;
unsigned long lastReconnectTry = 0;
int lastFeedKey = -1;

// ----------------------------------------------------------------------------
// 3. HARDWARE CONTROL ROUTINE
// ----------------------------------------------------------------------------
void feedNow() {
  Serial.println("[FEEDER] Dispensing food...");
  
  // Rotate servo to drop food
  feederServo.write(OPEN_ANGLE);
  delay(700); // Dispense duration

  // Return to closed position
  feederServo.write(CLOSED_ANGLE);
  delay(300);

  Serial.println("[FEEDER] Cycle complete.");
}

void publishStatus(const String& msg, bool retain = false) {
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_STATUS, msg.c_str(), retain);
    Serial.println("[MQTT] Published: " + msg);
  }
}

String timeString() {
  struct tm t;
  if (!getLocalTime(&t)) {
    return "time unknown";
  }
  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M", &t);
  return String(buf);
}

// ----------------------------------------------------------------------------
// 4. MQTT CALLBACK
// ----------------------------------------------------------------------------
void onMessage(char* topic, byte* payload, unsigned int len) {
  String msg = "";
  for (unsigned int i = 0; i < len; i++) {
    msg += (char)payload[i];
  }
  msg.trim();
  msg.toUpperCase();

  Serial.print("[MQTT] Received: ");
  Serial.println(msg);

  if (msg == "FEED") {
    feedRequested = true;
  }
}

void tryFeed(const char* source) {
  // 30s Cooldown guard
  if (millis() - lastFeedMs < COOLDOWN_MS && lastFeedMs != 0) {
    Serial.println("[FEEDER] Cooldown active, command rejected.");
    publishStatus("cooldown");
    return;
  }

  lastFeedMs = millis();
  Serial.println(String("[FEEDER] Executing feed: ") + source);
  feedNow();
  publishStatus("fed: " + timeString());
}

// ----------------------------------------------------------------------------
// 5. NETWORK CONNECTIVITY
// ----------------------------------------------------------------------------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("[WIFI] Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WIFI] Timeout, will retry in background.");
  }
}

void connectMqtt() {
  String clientId = "feeder-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("[MQTT] Connecting to broker.hivemq.com as ");
  Serial.print(clientId);
  Serial.print(" ... ");

  // Connect without password (public broker) with retained Last-Will
  if (mqtt.connect(clientId.c_str(), TOPIC_STATUS, 0, true, "offline")) {
    Serial.println("CONNECTED!");
    mqtt.subscribe(TOPIC_CMD);
    publishStatus("online", true);
  } else {
    Serial.print("FAILED, rc=");
    Serial.println(mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n==========================================");
  Serial.println("  ESP32 WiFi Fish Feeder (Ready to Run)   ");
  Serial.println("==========================================");

  // Attach servo to GPIO 18
  feederServo.attach(SERVO_PIN, 500, 2400);
  feederServo.write(CLOSED_ANGLE);

  // Connect to Wi-Fi
  connectWiFi();

  // NTP Time Sync
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");

  // Configure MQTT
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(256);
}

void loop() {
  // Reconnect Wi-Fi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // Non-blocking MQTT reconnect
  if (!mqtt.connected()) {
    if (millis() - lastReconnectTry > 5000) {
      lastReconnectTry = millis();
      connectMqtt();
    }
  } else {
    mqtt.loop();
  }

  // Handle feed command
  if (feedRequested) {
    feedRequested = false;
    tryFeed("website");
  }

  // Scheduled Feeding Check
  struct tm t;
  if (getLocalTime(&t)) {
    int key = t.tm_yday * 24 + t.tm_hour;
    for (int i = 0; i < NUM_FEEDS; i++) {
      if (t.tm_hour == FEED_HOURS[i] && t.tm_min == 0 && key != lastFeedKey) {
        lastFeedKey = key;
        Serial.println("[SCHEDULE] Scheduled feeding triggered!");
        tryFeed("schedule");
      }
    }
  }
}
