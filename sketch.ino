#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <ESP32Servo.h>

// ============================================================
// 3707ICT SMART HOME IoT AUTOMATION SYSTEM
// ============================================================

// --------------------------- Hardware pins ---------------------------

const int DHT_PIN = 15;
const int PIR_PIN = 27;
const int LDR_PIN = 34;

const int LIGHT_LED_PIN = 2;
const int CLIMATE_RELAY_PIN = 26;
const int BLINDS_SERVO_PIN = 18;

// --------------------------- Wi-Fi setup ------------------------------

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// ------------------------- Adafruit IO setup --------------------------

// Your Adafruit IO username
const char* AIO_USERNAME = "danielolver";

// IMPORTANT:
// Paste your NEW regenerated AIO key directly into Wokwi.
// Do not share it publicly.
const char* AIO_KEY = "YOUR_NEW_AIO_KEY";

const char* MQTT_SERVER = "io.adafruit.com";
const int MQTT_PORT = 1883;

// ---------------------- Automation configuration ---------------------

// Climate hysteresis
const float COOLING_ON_TEMP = 30.0;
const float COOLING_OFF_TEMP = 28.0;

const float HEATING_ON_TEMP = 16.0;
const float HEATING_OFF_TEMP = 18.0;

// Lighting hysteresis
// Light percentage:
// 0% = dark
// 100% = bright
const int LIGHT_ON_LEVEL = 35;
const int LIGHT_OFF_LEVEL = 45;

// Smart blinds thresholds
const int BLINDS_CLOSE_LEVEL = 70;
const int BLINDS_OPEN_LEVEL = 50;

const float BLINDS_CLOSE_TEMP = 27.0;
const float BLINDS_OPEN_TEMP = 25.0;

// -------------------------- Demo mode --------------------------------

// false = real assignment rule of 5 minutes
// true  = 10 second timeout for demonstration/testing

const bool DEMO_MODE = true;

const unsigned long OCCUPANCY_TIMEOUT_MS =
  DEMO_MODE ? 10000UL : 300000UL;

// ---------------------- Adaptive polling -----------------------------

// Faster when occupied or abnormal
const unsigned long ACTIVE_SENSOR_INTERVAL_MS = 2000UL;

// Slower when room is idle
const unsigned long NORMAL_SENSOR_INTERVAL_MS = 5000UL;

// Cloud update frequencies
const unsigned long ACTIVE_CLOUD_INTERVAL_MS = 5000UL;
const unsigned long NORMAL_CLOUD_INTERVAL_MS = 15000UL;

// Reconnection timing
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;
const unsigned long MQTT_RETRY_INTERVAL_MS = 5000UL;

// ============================================================
// CLIMATE MODES
// ============================================================

enum ClimateMode {
  CLIMATE_OFF,
  CLIMATE_COOLING,
  CLIMATE_HEATING
};

// ============================================================
// OBJECTS
// ============================================================

DHTesp dht;
Servo blindsServo;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ============================================================
// SENSOR / SYSTEM STATES
// ============================================================

float temperature = 24.0;
float humidity = 50.0;

int lightRaw = 0;
int lightPercent = 50;

bool pirMotion = false;
bool occupied = false;

bool roomLightOn = false;
bool blindsClosed = false;

ClimateMode climateMode = CLIMATE_OFF;

// ============================================================
// TIMING VARIABLES
// ============================================================

unsigned long lastMotionTime = 0;
unsigned long lastSensorRead = 0;
unsigned long lastCloudPublish = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;

// ============================================================
// HELPER FUNCTIONS
// ============================================================

const char* climateModeText() {

  if (climateMode == CLIMATE_COOLING) {
    return "COOLING";
  }

  if (climateMode == CLIMATE_HEATING) {
    return "HEATING";
  }

  return "OFF";
}

// ------------------------------------------------------------
// Determines whether faster polling should be used
// ------------------------------------------------------------

bool environmentAbnormal() {

  return (
    temperature >= COOLING_ON_TEMP ||
    temperature <= HEATING_ON_TEMP ||
    lightPercent <= LIGHT_ON_LEVEL
  );
}

// ============================================================
// WI-FI
// ============================================================

void maintainWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();

  if (
    lastWifiAttempt != 0 &&
    now - lastWifiAttempt < WIFI_RETRY_INTERVAL_MS
  ) {
    return;
  }

  lastWifiAttempt = now;

  Serial.println();
  Serial.println("Wi-Fi disconnected.");
  Serial.println("Attempting Wi-Fi connection...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD,
    6
  );
}

// ============================================================
// MQTT CONNECTION
// ============================================================

void maintainMQTT() {

  if (
    WiFi.status() != WL_CONNECTED ||
    mqttClient.connected()
  ) {
    return;
  }

  unsigned long now = millis();

  if (
    lastMqttAttempt != 0 &&
    now - lastMqttAttempt < MQTT_RETRY_INTERVAL_MS
  ) {
    return;
  }

  lastMqttAttempt = now;

  String clientId =
    "3707ICT-SmartHome-" +
    String(
      (uint32_t)ESP.getEfuseMac(),
      HEX
    );

  Serial.print(
    "Connecting to Adafruit IO MQTT... "
  );

  if (
    mqttClient.connect(
      clientId.c_str(),
      AIO_USERNAME,
      AIO_KEY
    )
  ) {

    Serial.println("CONNECTED");
  }

  else {

    Serial.print("FAILED - MQTT state: ");
    Serial.println(mqttClient.state());
  }
}

// ============================================================
// MQTT PUBLISHING
// ============================================================

void publishFeed(
  const char* feedName,
  const String& value
) {

  if (!mqttClient.connected()) {
    return;
  }

  String topic =
    String(AIO_USERNAME) +
    "/feeds/" +
    feedName;

  bool success =
    mqttClient.publish(
      topic.c_str(),
      value.c_str()
    );

  if (!success) {

    Serial.print(
      "Publish failed for feed: "
    );

    Serial.println(feedName);
  }
}

// ------------------------------------------------------------
// Publish all dashboard values
// ------------------------------------------------------------

void publishCloudData() {

  if (!mqttClient.connected()) {
    return;
  }

  publishFeed(
    "temperature",
    String(temperature, 1)
  );

  publishFeed(
    "humidity",
    String(humidity, 1)
  );

  publishFeed(
    "ambient-light",
    String(lightPercent)
  );

  publishFeed(
    "occupancy",
    occupied ? "1" : "0"
  );

  publishFeed(
    "room-light",
    roomLightOn ? "ON" : "OFF"
  );

  publishFeed(
    "climate-mode",
    climateModeText()
  );

  publishFeed(
    "blinds-position",
    blindsClosed ? "CLOSED" : "OPEN"
  );

  publishFeed(
    "system-status",
    "ONLINE"
  );

  Serial.println(
    "Cloud data published to Adafruit IO."
  );
}

// ============================================================
// SENSOR READING
// ============================================================

void readSensors() {

  // ---------------- DHT22 ----------------

  TempAndHumidity dhtData =
    dht.getTempAndHumidity();

  if (!isnan(dhtData.temperature)) {

    temperature =
      dhtData.temperature;
  }

  else {

    Serial.println(
      "WARNING: Invalid temperature reading."
    );
  }

  if (!isnan(dhtData.humidity)) {

    humidity =
      dhtData.humidity;
  }

  else {

    Serial.println(
      "WARNING: Invalid humidity reading."
    );
  }

  // ---------------- PIR ----------------

  pirMotion =
    digitalRead(PIR_PIN) == HIGH;

  if (pirMotion) {

    lastMotionTime = millis();

    occupied = true;
  }

  else if (
    occupied &&
    millis() - lastMotionTime >=
    OCCUPANCY_TIMEOUT_MS
  ) {

    occupied = false;

    Serial.println(
      "AUTOMATION: Occupancy timeout - room now unoccupied."
    );
  }

  // ---------------- LDR ----------------

  lightRaw =
    analogRead(LDR_PIN);

  /*
     Convert ESP32 ADC reading to a percentage.

     0%   = dark
     100% = bright
  */

  lightPercent =
    constrain(
      map(
        lightRaw,
        4095,
        0,
        0,
        100
      ),
      0,
      100
    );
}

// ============================================================
// CLIMATE CONTROL
// ============================================================

void updateClimateControl() {

  // ----------------------------------------------------------
  // Nobody in room -> climate OFF
  // ----------------------------------------------------------

  if (!occupied) {

    climateMode =
      CLIMATE_OFF;
  }

  // ----------------------------------------------------------
  // Climate currently OFF
  // ----------------------------------------------------------

  else if (
    climateMode ==
    CLIMATE_OFF
  ) {

    if (
      temperature >=
      COOLING_ON_TEMP
    ) {

      climateMode =
        CLIMATE_COOLING;

      Serial.println(
        "AUTOMATION: Cooling activated."
      );
    }

    else if (
      temperature <=
      HEATING_ON_TEMP
    ) {

      climateMode =
        CLIMATE_HEATING;

      Serial.println(
        "AUTOMATION: Heating activated."
      );
    }
  }

  // ----------------------------------------------------------
  // Cooling hysteresis
  // ----------------------------------------------------------

  else if (
    climateMode ==
      CLIMATE_COOLING &&
    temperature <=
      COOLING_OFF_TEMP
  ) {

    climateMode =
      CLIMATE_OFF;

    Serial.println(
      "AUTOMATION: Cooling deactivated."
    );
  }

  // ----------------------------------------------------------
  // Heating hysteresis
  // ----------------------------------------------------------

  else if (
    climateMode ==
      CLIMATE_HEATING &&
    temperature >=
      HEATING_OFF_TEMP
  ) {

    climateMode =
      CLIMATE_OFF;

    Serial.println(
      "AUTOMATION: Heating deactivated."
    );
  }

  // Relay represents active climate system
  digitalWrite(
    CLIMATE_RELAY_PIN,
    climateMode == CLIMATE_OFF
      ? LOW
      : HIGH
  );
}

// ============================================================
// SMART LIGHTING
// ============================================================

void updateRoomLighting() {

  // Empty room -> light OFF

  if (!occupied) {

    roomLightOn = false;
  }

  // Dark + occupied -> light ON

  else if (
    !roomLightOn &&
    lightPercent <=
      LIGHT_ON_LEVEL
  ) {

    roomLightOn = true;

    Serial.println(
      "AUTOMATION: Smart lighting ON."
    );
  }

  // Bright enough -> light OFF

  else if (
    roomLightOn &&
    lightPercent >=
      LIGHT_OFF_LEVEL
  ) {

    roomLightOn = false;

    Serial.println(
      "AUTOMATION: Smart lighting OFF."
    );
  }

  digitalWrite(
    LIGHT_LED_PIN,
    roomLightOn
      ? HIGH
      : LOW
  );
}

// ============================================================
// SMART MOTORISED BLINDS
// ============================================================

void updateBlinds() {

  /*
     Edge intelligence:

     Close blinds only when the room is BOTH
     bright and warm.

     This reduces solar heat gain while avoiding
     unnecessary blind movement.
  */

  if (
    !blindsClosed &&
    lightPercent >=
      BLINDS_CLOSE_LEVEL &&
    temperature >=
      BLINDS_CLOSE_TEMP
  ) {

    blindsClosed = true;

    blindsServo.write(90);

    Serial.println(
      "AUTOMATION: Blinds CLOSED."
    );
  }

  /*
     Re-open if it becomes darker OR
     the room cools down.
  */

  else if (
    blindsClosed &&
    (
      lightPercent <=
        BLINDS_OPEN_LEVEL ||
      temperature <=
        BLINDS_OPEN_TEMP
    )
  ) {

    blindsClosed = false;

    blindsServo.write(0);

    Serial.println(
      "AUTOMATION: Blinds OPEN."
    );
  }
}

// ============================================================
// APPLY ALL AUTOMATION
// ============================================================

void applyAutomationRules() {

  updateClimateControl();

  updateRoomLighting();

  updateBlinds();
}

// ============================================================
// SERIAL MONITOR STATUS
// ============================================================

void printSystemState() {

  Serial.println();

  Serial.println(
    "=================================================="
  );

  Serial.println(
    "         SMART HOME SYSTEM STATUS"
  );

  Serial.println(
    "=================================================="
  );

  Serial.print("Temperature: ");
  Serial.print(temperature, 1);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(humidity, 1);
  Serial.println(" %");

  Serial.print("Ambient Light: ");
  Serial.print(lightPercent);
  Serial.println(" %");

  Serial.print("Raw LDR Value: ");
  Serial.println(lightRaw);

  Serial.print("PIR Motion: ");

  Serial.println(
    pirMotion
      ? "DETECTED"
      : "CLEAR"
  );

  Serial.print("Occupancy: ");

  Serial.println(
    occupied
      ? "OCCUPIED"
      : "UNOCCUPIED"
  );

  Serial.print("Room Light: ");

  Serial.println(
    roomLightOn
      ? "ON"
      : "OFF"
  );

  Serial.print("Climate Mode: ");

  Serial.println(
    climateModeText()
  );

  Serial.print("Blinds Position: ");

  Serial.println(
    blindsClosed
      ? "CLOSED"
      : "OPEN"
  );

  Serial.print("Wi-Fi: ");

  Serial.println(
    WiFi.status() ==
      WL_CONNECTED
      ? "CONNECTED"
      : "DISCONNECTED"
  );

  Serial.print("MQTT / Cloud: ");

  Serial.println(
    mqttClient.connected()
      ? "CONNECTED"
      : "OFFLINE"
  );

  Serial.print("Polling Mode: ");

  Serial.println(
    (
      occupied ||
      environmentAbnormal()
    )
      ? "FAST"
      : "NORMAL"
  );

  Serial.println(
    "=================================================="
  );

  Serial.println();
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println(
    "3707ICT Smart Home IoT Automation System"
  );

  Serial.println(
    "System starting..."
  );

  // ---------------- Inputs ----------------

  pinMode(
    PIR_PIN,
    INPUT
  );

  pinMode(
    LDR_PIN,
    INPUT
  );

  // ---------------- Outputs ----------------

  pinMode(
    LIGHT_LED_PIN,
    OUTPUT
  );

  pinMode(
    CLIMATE_RELAY_PIN,
    OUTPUT
  );

  digitalWrite(
    LIGHT_LED_PIN,
    LOW
  );

  digitalWrite(
    CLIMATE_RELAY_PIN,
    LOW
  );

  // ---------------- DHT22 ----------------

  dht.setup(
    DHT_PIN,
    DHTesp::DHT22
  );

  // ---------------- Servo ----------------

  blindsServo.setPeriodHertz(50);

  blindsServo.attach(
    BLINDS_SERVO_PIN,
    500,
    2400
  );

  blindsServo.write(0);

  // ---------------- MQTT ----------------

  mqttClient.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );

  mqttClient.setBufferSize(256);

  // ---------------- Wi-Fi ----------------

  maintainWiFi();

  if (DEMO_MODE) {

    Serial.println(
      "DEMO MODE ACTIVE: occupancy timeout = 10 seconds"
    );
  }

  else {

    Serial.println(
      "NORMAL MODE: occupancy timeout = 5 minutes"
    );
  }

  // Initial sensor reading
  readSensors();

  applyAutomationRules();

  printSystemState();

  Serial.println(
    "System ready."
  );
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

  // Maintain network connections
  maintainWiFi();

  maintainMQTT();

  mqttClient.loop();

  unsigned long now =
    millis();

  // ==========================================================
  // ADAPTIVE SENSOR POLLING
  // ==========================================================

  unsigned long sensorInterval =
    (
      occupied ||
      environmentAbnormal()
    )
      ? ACTIVE_SENSOR_INTERVAL_MS
      : NORMAL_SENSOR_INTERVAL_MS;

  if (
    now - lastSensorRead >=
    sensorInterval
  ) {

    lastSensorRead = now;

    readSensors();

    applyAutomationRules();

    printSystemState();
  }

  // ==========================================================
  // ADAPTIVE CLOUD UPDATES
  // ==========================================================

  unsigned long cloudInterval =
    (
      occupied ||
      environmentAbnormal()
    )
      ? ACTIVE_CLOUD_INTERVAL_MS
      : NORMAL_CLOUD_INTERVAL_MS;

  if (
    now - lastCloudPublish >=
    cloudInterval
  ) {

    lastCloudPublish = now;

    publishCloudData();
  }
}
