#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>

// ====== USER CONFIG ======
const char* WIFI_SSID = "test";
const char* WIFI_PASS = "test123456";
const char* MQTT_HOST = "192.168.1.159"; // PC's LAN IP (NOT localhost)
const uint16_t MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "esp32-attendance-01";
const char* MQTT_TOPIC = "attendance/events";
const char* DEVICE_NAME = "esp32-lab-1";

// Malaysia time (UTC+8)
const long GMT_OFFSET_SEC = 8 * 3600;
const int  DST_OFFSET_SEC = 0;
const char* NTP_SERVER    = "pool.ntp.org";

// OLED
#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);

// Fingerprint on UART2
#define FP_RX 16
#define FP_TX 17
HardwareSerial FPSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FPSerial);

// Networking
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ID -> Name table
struct IdName { uint16_t id; const char* name; };
IdName idmap[] = {
  {1, "Haikal"},
  {2, "Siti"},
  {3, "Ravi"},
};
const size_t idmapN = sizeof(idmap)/sizeof(idmap[0]);
const char* nameFor(uint16_t id){ for(size_t i=0;i<idmapN;i++) if(idmap[i].id==id) return idmap[i].name; return nullptr; }

// OLED text helper
void oledCentered(const String& l1,const String& l2="",const String& l3=""){
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  auto line=[&](const String&s,int y){ int16_t x1,y1; uint16_t w,h; display.getTextBounds(s,0,0,&x1,&y1,&w,&h); display.setCursor((OLED_W-w)/2, y); display.print(s); };
  if(l1.length()) line(l1,10); if(l2.length()) line(l2,30); if(l3.length()) line(l3,50); display.display();
}
String iso8601Now(){ struct tm ti; if(!getLocalTime(&ti,1500)) return ""; char buf[40]; strftime(buf,sizeof(buf),"%Y-%m-%dT%H:%M:%S%z",&ti); String s(buf); if(s.length()>=24) s=s.substring(0,22)+":"+s.substring(22); return s; }

// Non-blocking WiFi/MQTT
unsigned long lastWifiTry=0,lastMqttTry=0; const unsigned long WIFI_RETRY_MS=5000,MQTT_RETRY_MS=5000;
void wifiPump(){ if(WiFi.status()==WL_CONNECTED) return; unsigned long now=millis(); if(now-lastWifiTry<WIFI_RETRY_MS) return; lastWifiTry=now; WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID,WIFI_PASS); }
void mqttPump(){ if(WiFi.status()!=WL_CONNECTED) return; if(mqtt.connected()){ mqtt.loop(); return; } unsigned long now=millis(); if(now-lastMqttTry<MQTT_RETRY_MS) return; lastMqttTry=now; mqtt.setServer(MQTT_HOST,MQTT_PORT); (void)mqtt.connect(MQTT_CLIENT_ID); }
bool mqttSafePublish(const char* topic,const String& payload,bool retained=false){ if(!mqtt.connected()) return false; return mqtt.publish(topic,payload.c_str(),retained); }

// Anti-duplicate gate
uint16_t lastId=0; unsigned long lastSentMs=0; const unsigned long DEDUP_WINDOW_MS=8000;

void setup(){
  Serial.begin(115200); delay(150);
  Wire.begin(21,22); display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR); oledCentered("Booting...","Initializing");
  FPSerial.begin(57600, SERIAL_8N1, FP_RX, FP_TX); finger.begin(57600);
  if(finger.verifyPassword()){ Serial.println("Fingerprint sensor OK"); } else { Serial.println("Fingerprint sensor NOT found"); }
  finger.getTemplateCount(); Serial.printf("Templates: %d\n", finger.templateCount);
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
  oledCentered("Ready.","Place finger...");
}

void loop(){
  wifiPump(); mqttPump();

  uint8_t p = finger.getImage();
  if(p==FINGERPRINT_NOFINGER){ oledCentered("Scanning..."); delay(120); return; }
  if(p!=FINGERPRINT_OK){ oledCentered("Try again","(noisy/dirty)"); delay(250); return; }

  p = finger.image2Tz();
  if(p!=FINGERPRINT_OK){ oledCentered("Scan poor","Place flat"); delay(350); return; }

  p = finger.fingerFastSearch();
  if(p==FINGERPRINT_OK){
    uint16_t id=finger.fingerID, conf=finger.confidence; const char* nmC=nameFor(id); String nm = nmC?String(nmC):"Unknown";
    oledCentered(String("Approved ")+nm+" enter", String("ID#")+id);

    unsigned long now = millis();
    if(!(id==lastId && (now-lastSentMs)<DEDUP_WINDOW_MS)){
      String ts=iso8601Now();
      String payload = String("{\"timestamp\":\"")+ts+"\",\"device\":\""+DEVICE_NAME+"\",\"finger_id\":"+id+",\"name\":\""+nm+"\",\"confidence\":"+conf+",\"status\":\"present\"}";
      bool sent = mqttSafePublish(MQTT_TOPIC, payload, true);
      Serial.println(sent? "Data Received OK":"MQTT publish SKIPPED (not connected)");
      lastId=id; lastSentMs=now;
    }
    delay(1000);
  } else if(p==FINGERPRINT_NOTFOUND){
    oledCentered("scanning..","Unknown"); delay(700);
    /*
    String ts=iso8601Now();
    String payload=String("{\"timestamp\":\"")+ts+"\",\"device\":\""+DEVICE_NAME+"\",\"finger_id\":-1,\"name\":\"Unknown\",\"confidence\":0,\"status\":\"unknown\"}";
    mqttSafePublish(MQTT_TOPIC,payload,true);
    */
  } else {
    oledCentered("Error","Search fail"); delay(600);
  }
}
