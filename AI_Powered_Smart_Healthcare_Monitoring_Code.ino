#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <MAX30105.h>
#include "heartRate.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------- WiFi ----------
#define WIFI_SSID "Oneplus Nord 4"
#define WIFI_PASSWORD "xxxxxxxxxxxxxxx"

// ---------- MQTT ----------
const char* mqtt_server = "9a6560927d574292b026839c6471dc26.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "CHIRAG";
const char* mqtt_pass = "CHIRAGs#1234";
#define MQTT_TOPIC "hospital/patient1"

// ---------- Clients ----------
WiFiClientSecure espClient;
PubSubClient client(espClient);

// ---------- Sensors ----------
MAX30105 particleSensor;
#define ONE_WIRE_BUS 4
#define buzzer 5
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// ---------- SIM800 ----------
#define SIM800_RX 16
#define SIM800_TX 17
HardwareSerial sim800(2);

// ---------- Variables ----------
uint32_t lastBeat = 0;
float heartRate = 0;
float spO2 = 0;
int beatThreshold = 2000;

const float TEMP_LOW = 80.0;
const float TEMP_HIGH = 100.4;
const float HR_LOW = 50;
const float HR_HIGH = 110;

bool hrLowSent = false;
bool hrHighSent = false;
bool tempLowSent = false;
bool tempHighSent = false;

String phoneNumber = "+91xxxxxxxxxxxxx";

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);

  sim800.begin(9600, SERIAL_8N1, SIM800_RX, SIM800_TX);
  initSIM800();

  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30105 not found!");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeGreen(0);

  pinMode(buzzer, OUTPUT);
  sensors.begin();
}

// ================= LOOP =================
void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  readHeartRateOxygen();
  float temperature = readTemperature();

  sendMQTT(heartRate, spO2, temperature);
  checkAlerts(heartRate, temperature);

  delay(1000);
}

// ================= MQTT =================
void reconnectMQTT() {
  while (!client.connected()) {
    String clientId = "ESP32-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("MQTT Connected");
    } else {
      delay(3000);
    }
  }
}

void sendMQTT(float hr, float spo2, float temp) {
  String status = (hr < HR_LOW || hr > HR_HIGH || temp < TEMP_LOW || temp > TEMP_HIGH) ? "EMERGENCY" : "NORMAL";

  String payload = "{";
  payload += "\"heartRate\":" + String(hr) + ",";
  payload += "\"SpO2\":" + String(spo2) + ",";
  payload += "\"temperature\":" + String(temp) + ",";
  payload += "\"status\":\"" + status + "\"}";
  
  client.publish(MQTT_TOPIC, payload.c_str());
}

// ================= SENSORS =================
void readHeartRateOxygen() {
  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();

  if (irValue < 5000) {
    heartRate = 0;
    spO2 = 0;
    resetAlerts();
    return;
  }

  if (irValue > beatThreshold) {
    long beatDuration = millis() - lastBeat;
    if (beatDuration > 300) {
      heartRate = 60.0 / (beatDuration / 1000.0);
      lastBeat = millis();
    }
  }

  float ratio = (float)redValue / (float)irValue;
  spO2 = constrain(110.0 - (25.0 * ratio), 90, 100);
}

float readTemperature() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  if (tempC == DEVICE_DISCONNECTED_C) tempC = 0;
  return tempC * 9.0 / 5.0 + 32.0 + 3.5;
}

// ================= ALERTS =================
void checkAlerts(float hr, float temp) {
  bool alert = false;

  if (hr > 0 && hr < HR_LOW) { if (!hrLowSent) sendSMS("LOW HR"); hrLowSent = true; alert = true; } else hrLowSent = false;
  if (hr > HR_HIGH) { if (!hrHighSent) sendSMS("HIGH HR"); hrHighSent = true; alert = true; } else hrHighSent = false;
  if (temp > 0 && temp < TEMP_LOW) { if (!tempLowSent) sendSMS("LOW TEMP"); tempLowSent = true; alert = true; } else tempLowSent = false;
  if (temp > TEMP_HIGH) { if (!tempHighSent) sendSMS("HIGH TEMP"); tempHighSent = true; alert = true; } else tempHighSent = false;

  digitalWrite(buzzer, alert ? HIGH : LOW);
}

void resetAlerts() {
  hrLowSent = hrHighSent = tempLowSent = tempHighSent = false;
}

// ================= SIM800 =================
void initSIM800() {
  sendAT("AT");
  sendAT("AT+CMGF=1");
}

void sendAT(String cmd) {
  sim800.println(cmd);
  delay(500);
}

void sendSMS(String msg) {
  sim800.print("AT+CMGS=\"");
  sim800.print(phoneNumber);
  sim800.println("\"");
  delay(500);
  sim800.print(msg);
  delay(500);
  sim800.write(26);
}