#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <ESP32Servo.h>

// --------------------------- Hardware pins ---------------------------
const int DHT_PIN = 15;
const int PIR_PIN = 27;
const int LDR_PIN = 34;
const int LIGHT_LED_PIN = 2;
const int CLIMATE_RELAY_PIN = 26;
const int BLINDS_SERVO_PIN = 18;

// ------------------------- Adafruit IO setup -------------------------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* AIO_USERNAME = "danielolver";
const char* AIO_KEY = "aio_dKJi22zxKXXDXigs7WCIIUWZ05X4";
const char* MQTT_SERVER = "io.adafruit.com";
const int MQTT_PORT = 1883;

// ---------------------- Automation configuration --------------------
const float COOLING_ON_TEMP = 30.0;
const float COOLING_OFF_TEMP = 28.0;
const float HEATING_ON_TEMP = 16.0;
const float HEATING_OFF_TEMP = 18.0;

// Light is expressed as a percentage: 0% = dark, 100% = bright.
const int LIGHT_ON_LEVEL = 35;
const int LIGHT_OFF_LEVEL = 45;
const int BLINDS_CLOSE_LEVEL = 70;
const int BLINDS_OPEN_LEVEL = 50;
const float BLINDS_CLOSE_TEMP = 27.0;
const float BLINDS_OPEN_TEMP = 25.0;

/* Change DEMO_MODE to true for a
 ten-second classroom demonstration of the no-motion rule. */
 
const bool DEMO_MODE = false;
const unsigned long OCCUPANCY_TIMEOUT_MS =
  DEMO_MODE ? 10000UL : 300000UL;

const unsigned long ACTIVE_SENSOR_INTERVAL_MS = 2000UL;
const unsigned long NORMAL_SENSOR_INTERVAL_MS = 5000UL;
const unsigned long ACTIVE_CLOUD_INTERVAL_MS = 2000UL;
const unsigned long NORMAL_CLOUD_INTERVAL_MS = 30000UL;
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;
const unsigned long MQTT_RETRY_INTERVAL_MS = 5000UL;

enum ClimateMode {
  CLIMATE_OFF,
  CLIMATE_COOLING,
  CLIMATE_HEATING
};

DHTesp dht;
Servo blindsServo;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

float temperature = 24.0;
float humidity = 50.0;
int lightRaw = 0;
int lightPercent = 50;
bool pirMotion = false;
bool occupied = false;
bool roomLightOn = false;
bool blindsClosed = false;
ClimateMode climateMode = CLIMATE_OFF;

unsigned long lastMotionTime = 0;
unsigned long lastSensorRead = 0;
unsigned long lastCloudPublish = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;

/* bool cloudConfigured() {
  return String(AIO_USERNAME) != "danielolver" &&
         String(AIO_KEY) != "aio_dKJi22zxKXXDXigs7WCIIUWZ05X4";
}

*/

bool cloudConfigured() {
  return true;
}

const char* climateModeText() {
  if (climateMode == CLIMATE_COOLING) return "COOLING";
  if (climateMode == CLIMATE_HEATING) return "HEATING";
  return "OFF";
}

bool environmentAbnormal() {
  return temperature >= COOLING_ON_TEMP ||
         temperature <= HEATING_ON_TEMP ||
         lightPercent <= LIGHT_ON_LEVEL;
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if (lastWifiAttempt != 0 && now - lastWifiAttempt < WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  lastWifiAttempt = now;
  Serial.println("Starting Wokwi Wi-Fi connection attempt");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);
}

void connectMqttNonBlocking() {
  if (!cloudConfigured() || WiFi.status() != WL_CONNECTED || mqttClient.connected()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastMqttAttempt < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttAttempt = now;

  String clientId = "smart-home-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("Connecting to Adafruit IO MQTT... ");

  if (mqttClient.connect(clientId.c_str(), AIO_USERNAME, AIO_KEY)) {
    Serial.println("connected");
  } else {
    Serial.print("failed, state=");
    Serial.println(mqttClient.state());
  }
}

void publishFeed(const char* feedName, const String& value) {
  if (!mqttClient.connected()) return;

  String topic = String(AIO_USERNAME) + "/feeds/" + feedName;
  mqttClient.publish(topic.c_str(), value.c_str(), true);
}

void publishCloudData() {
  if (!mqttClient.connected()) return;

  publishFeed("temperature", String(temperature, 1));
  publishFeed("humidity", String(humidity, 1));
  publishFeed("occupancy", occupied ? "1" : "0");
  publishFeed("ambient-light", String(lightPercent));
  publishFeed("room-light", roomLightOn ? "ON" : "OFF");
  publishFeed("climate-mode", climateModeText());
  publishFeed("blinds-position", blindsClosed ? "CLOSED" : "OPEN");
  publishFeed("system-status", "ONLINE");

  Serial.println("Sensor and actuator data published to Adafruit IO");
}

void readSensors() {
  TempAndHumidity dhtData = dht.getTempAndHumidity();

  if (!isnan(dhtData.temperature)) temperature = dhtData.temperature;
  if (!isnan(dhtData.humidity)) humidity = dhtData.humidity;

  pirMotion = digitalRead(PIR_PIN) == HIGH;
  lightRaw = analogRead(LDR_PIN);

  // Wokwi's photoresistor output increases as the scene becomes darker.
  lightPercent = constrain(map(lightRaw, 4095, 0, 0, 100), 0, 100);

  if (pirMotion) {
    lastMotionTime = millis();
    occupied = true;
  } else if (occupied && millis() - lastMotionTime >= OCCUPANCY_TIMEOUT_MS) {
    occupied = false;
  }
}

void updateClimateControl() {
  if (!occupied) {
    climateMode = CLIMATE_OFF;
  } else if (climateMode == CLIMATE_OFF) {
    if (temperature >= COOLING_ON_TEMP) {
      climateMode = CLIMATE_COOLING;
    } else if (temperature <= HEATING_ON_TEMP) {
      climateMode = CLIMATE_HEATING;
    }
  } else if (climateMode == CLIMATE_COOLING && temperature <= COOLING_OFF_TEMP) {
    climateMode = CLIMATE_OFF;
  } else if (climateMode == CLIMATE_HEATING && temperature >= HEATING_OFF_TEMP) {
    climateMode = CLIMATE_OFF;
  }

  digitalWrite(CLIMATE_RELAY_PIN,
               climateMode == CLIMATE_OFF ? LOW : HIGH);
}

void updateRoomLighting() {
  if (!occupied) {
    roomLightOn = false;
  } else if (!roomLightOn && lightPercent <= LIGHT_ON_LEVEL) {
    roomLightOn = true;
  } else if (roomLightOn && lightPercent >= LIGHT_OFF_LEVEL) {
    roomLightOn = false;
  }

  digitalWrite(LIGHT_LED_PIN, roomLightOn ? HIGH : LOW);
}

void updateBlinds() {
  // Context-aware decision: close the blinds only when it is both bright
  // and warm, reducing heat gain without blocking useful daylight.
  if (!blindsClosed &&
      lightPercent >= BLINDS_CLOSE_LEVEL &&
      temperature >= BLINDS_CLOSE_TEMP) {
    blindsClosed = true;
    blindsServo.write(90);
  } else if (blindsClosed &&
             (lightPercent <= BLINDS_OPEN_LEVEL ||
              temperature <= BLINDS_OPEN_TEMP)) {
    blindsClosed = false;
    blindsServo.write(0);
  }
}

void applyAutomationRules() {
  updateClimateControl();
  updateRoomLighting();
  updateBlinds();
}

void printSystemState() {
  Serial.println("--------------------------------------------------");
  Serial.printf("Temperature: %.1f C | Humidity: %.1f %%\n",
                temperature, humidity);
  Serial.printf("Light: %d %% | PIR: %s | Occupied: %s\n",
                lightPercent,
                pirMotion ? "MOTION" : "CLEAR",
                occupied ? "YES" : "NO");
  Serial.printf("Room light: %s | Climate: %s | Blinds: %s\n",
                roomLightOn ? "ON" : "OFF",
                climateModeText(),
                blindsClosed ? "CLOSED" : "OPEN");
  Serial.printf("Cloud: %s | Polling: %s\n",
                mqttClient.connected() ? "CONNECTED" : "OFFLINE",
                (occupied || environmentAbnormal()) ? "FAST" : "NORMAL");
}

void setup() {

  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(LIGHT_LED_PIN, OUTPUT);
  pinMode(CLIMATE_RELAY_PIN, OUTPUT);

  digitalWrite(LIGHT_LED_PIN, LOW);
  digitalWrite(CLIMATE_RELAY_PIN, LOW);

  dht.setup(DHT_PIN, DHTesp::DHT22);
  blindsServo.setPeriodHertz(50);
  blindsServo.attach(BLINDS_SERVO_PIN, 500, 2400);
  blindsServo.write(0);

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setBufferSize(256);

  Serial.println("Smart Home IoT Automation System starting");
  if (DEMO_MODE) Serial.println("DEMO MODE: occupancy timeout is 10 seconds");

  maintainWiFi();
  readSensors();
  applyAutomationRules();
  printSystemState();
}

void loop() {
  maintainWiFi();
  connectMqttNonBlocking();
  mqttClient.loop();

  unsigned long now = millis();
  unsigned long sensorInterval =
    (occupied || environmentAbnormal())
      ? ACTIVE_SENSOR_INTERVAL_MS
      : NORMAL_SENSOR_INTERVAL_MS;

  if (now - lastSensorRead >= sensorInterval) {
    lastSensorRead = now;
    readSensors();
    applyAutomationRules();
    printSystemState();
  }

  unsigned long cloudInterval =
    (occupied || environmentAbnormal())
      ? ACTIVE_CLOUD_INTERVAL_MS
      : NORMAL_CLOUD_INTERVAL_MS;

  if (now - lastCloudPublish >= cloudInterval) {
    lastCloudPublish = now;
    publishCloudData();
  }
}
