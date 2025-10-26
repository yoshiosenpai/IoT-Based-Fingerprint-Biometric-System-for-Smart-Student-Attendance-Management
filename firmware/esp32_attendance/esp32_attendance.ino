#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ====== WiFi & MQTT Config ======
const char* WIFI_SSID = "test";
const char* WIFI_PASS = "test123456";
const char* MQTT_HOST = "192.168.137.1";   // PC hotspot IP
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "attendance/events";
const char* DEVICE_NAME = "esp32-lab-1";

// ====== Fingerprint ======
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// ====== OLED ======
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ====== MQTT Client ======
WiFiClient espClient;
PubSubClient client(espClient);

// ====== Function Prototypes ======
void connectWiFi();
void connectMQTT();
void showMessage(String msg);
void publishAttendance(uint8_t fid, String name);

// ====== Setup ======
void setup() {
  Serial.begin(115200);
  delay(1000);

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
    while (1);
  }
  showMessage("Booting...");

  // Fingerprint init
  mySerial.begin(57600, SERIAL_8N1, 16, 17);
  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor OK");
  } else {
    showMessage("No Fingerprint!");
    while (1);
  }

  // WiFi + MQTT
  connectWiFi();
  client.setServer(MQTT_HOST, MQTT_PORT);

  showMessage("Ready!");
}

// ====== Loop ======
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!client.connected()) connectMQTT();
  client.loop();

  getFingerprintID();
}

// ====== WiFi Connect ======
void connectWiFi() {
  showMessage("Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint8_t attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    delay(500);
    Serial.print(".");
    attempt++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.println(WiFi.localIP());
    showMessage("WiFi Connected!");
  } else {
    showMessage("WiFi Failed!");
  }
}

// ====== MQTT Connect ======
void connectMQTT() {
  showMessage("Connecting MQTT...");
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect(DEVICE_NAME)) {
      Serial.println("connected!");
      showMessage("MQTT Connected!");
    } else {
      Serial.print("failed, state=");
      Serial.println(client.state());
      showMessage("MQTT Retry...");
      delay(2000);
    }
  }
}

// ====== Show message on OLED ======
void showMessage(String msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 25);
  display.println(msg);
  display.display();
}

// ====== Fingerprint scan ======
void getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return;

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    Serial.print("Found ID: "); Serial.println(finger.fingerID);
    String studentName = "";
    switch (finger.fingerID) {
      case 1: studentName = "Ali"; break;
      case 2: studentName = "Siti"; break;
      case 3: studentName = "Ahmad"; break;
      default: studentName = "Unknown"; break;
    }

    showMessage("Welcome " + studentName);
    publishAttendance(finger.fingerID, studentName);
    delay(2000);
  } else {
    showMessage("No Match!");
    delay(1000);
  }
}

// ====== Publish attendance ======
void publishAttendance(uint8_t fid, String name) {
  if (!client.connected()) {
    Serial.println("MQTT not connected, skip publish");
    return;
  }

  String payload = "{";
  payload += "\"device\":\"" + String(DEVICE_NAME) + "\",";
  payload += "\"finger_id\":" + String(fid) + ",";
  payload += "\"name\":\"" + name + "\",";
  payload += "\"status\":\"present\"";
  payload += "}";

  client.publish(MQTT_TOPIC, payload.c_str());
  Serial.println("Published: " + payload);
}
