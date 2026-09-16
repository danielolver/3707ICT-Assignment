#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <ESP32Servo.h>

// ============================================================
// 3707ICT SMART HOME IoT AUTOMATION SYSTEM
// ============================================================
//
// Sensors:
//   DHT22  -> Temperature / Humidity
//   PIR    -> Motion / Occupancy
//   LDR    -> Ambient Light
//
// Actuators:
//   LED    -> Room Lighting
//   Relay  -> Climate Control
//   Servo  -> Motorised Blinds
//
// Intelligent Features:
//   - Occupancy-aware climate control
//   - Occupancy-aware smart lighting
//   - Temperature + light smart blinds
//   - Hysteresis
//   - Adaptive sensor polling
//   - Adaptive cloud update frequency
//
// Communication:
//   Wi-Fi + MQTT + Adafruit IO
// ============================================================


// ============================================================
// HARDWARE PIN DEFINITIONS
// ============================================================

const int DHT_PIN = 15;
const int PIR_PIN = 27;
const int LDR_PIN = 34;

const int LIGHT_LED_PIN = 2;
const int CLIMATE_RELAY_PIN = 26;
const int BLINDS_SERVO_PIN = 18;


// ============================================================
// WI-FI CONFIGURATION
// ============================================================

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";


// ============================================================
// ADAFRUIT IO / MQTT CONFIGURATION
// ============================================================

const char* AIO_USERNAME = "danielolver";

const char* AIO_KEY = "aio_dKJi22zxKXXDXigs7WCIIUWZ05X4";

const char* MQTT_SERVER = "io.adafruit.com";
const int MQTT_PORT = 1883;


// ============================================================
// TEMPERATURE CONTROL
// ============================================================

// Cooling hysteresis
const float COOLING_ON_TEMP = 30.0;
const float COOLING_OFF_TEMP = 28.0;

// Heating hysteresis
const float HEATING_ON_TEMP = 16.0;
const float HEATING_OFF_TEMP = 18.0;


// ============================================================
// SMART LIGHTING
// ============================================================

// Converted light percentage:
// 0%   = dark
// 100% = bright

const int LIGHT_ON_LEVEL = 35;
const int LIGHT_OFF_LEVEL = 45;


// ============================================================
// SMART BLINDS
// ============================================================

// Blinds close only when:
//   Light >= 70%
// AND
//   Temperature >= 27°C

const int BLINDS_CLOSE_LEVEL = 70;
const int BLINDS_OPEN_LEVEL = 50;

const float BLINDS_CLOSE_TEMP = 27.0;
const float BLINDS_OPEN_TEMP = 25.0;


// ============================================================
// OCCUPANCY CONFIGURATION
// ============================================================

// true  = 10 second timeout for demonstration
// false = real 5 minute assignment rule

const bool DEMO_MODE = true;

const unsigned long OCCUPANCY_TIMEOUT_MS =
  DEMO_MODE
    ? 10000UL
    : 300000UL;


// ============================================================
// ADAPTIVE POLLING
// ============================================================

// Sensor readings:
//
// Active / abnormal conditions = faster
// Normal / unoccupied = slower

const unsigned long ACTIVE_SENSOR_INTERVAL_MS = 2000UL;
const unsigned long NORMAL_SENSOR_INTERVAL_MS = 5000UL;

// Cloud publishing is deliberately slower than sensor polling.
// Local automation does NOT depend on cloud updates.

const unsigned long ACTIVE_CLOUD_INTERVAL_MS = 20000UL;
const unsigned long NORMAL_CLOUD_INTERVAL_MS = 60000UL;


// ============================================================
// CONNECTION RETRY INTERVALS
// ============================================================

const unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;
const unsigned long MQTT_RETRY_INTERVAL_MS = 5000UL;


// ============================================================
// CLIMATE MODE
// ============================================================

enum ClimateMode {
  CLIMATE_OFF,
  CLIMATE_COOLING,
  CLIMATE_HEATING
};


// ============================================================
// SYSTEM OBJECTS
// ============================================================

DHTesp dht;
Servo blindsServo;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);


// ============================================================
// SENSOR VALUES
// ============================================================

float temperature = 24.0;
float humidity = 50.0;

int lightRaw = 0;
int lightPercent = 50;

bool pirMotion = false;


// ============================================================
// SYSTEM STATES
// ============================================================

bool occupied = false;
bool roomLightOn = false;
bool blindsClosed = false;

bool dhtHealthy = true;

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
// CLIMATE MODE TEXT
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


// ============================================================
// SYSTEM STATUS TEXT
// ============================================================

const char* systemStatusText() {

  if (!dhtHealthy) {
    return "SENSOR FAULT";
  }

  if (WiFi.status() != WL_CONNECTED) {
    return "LOCAL MODE";
  }

  if (!mqttClient.connected()) {
    return "CLOUD OFFLINE";
  }

  return "ONLINE";
}


// ============================================================
// DETERMINE IF CONDITIONS REQUIRE FAST POLLING
// ============================================================

bool environmentAbnormal() {

  return (
    temperature >= COOLING_ON_TEMP ||
    temperature <= HEATING_ON_TEMP ||
    lightPercent <= LIGHT_ON_LEVEL ||
    lightPercent >= BLINDS_CLOSE_LEVEL
  );
}


// ============================================================
// WI-FI MANAGEMENT
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
  Serial.println("NETWORK: Attempting Wi-Fi connection...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD,
    6
  );
}


// ============================================================
// MQTT MANAGEMENT
// ============================================================

void maintainMQTT() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqttClient.connected()) {
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
    "MQTT: Connecting to Adafruit IO... "
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

    Serial.print("FAILED (state ");
    Serial.print(mqttClient.state());
    Serial.println(")");
  }
}


// ============================================================
// MQTT FEED PUBLISH FUNCTION
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
      "MQTT WARNING: Failed to publish "
    );

    Serial.println(feedName);
  }
}


// ============================================================
// PUBLISH DATA TO ADAFRUIT IO
// ============================================================

void publishCloudData() {

  if (!mqttClient.connected()) {

    Serial.println(
      "CLOUD: MQTT offline - local automation continues."
    );

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
    systemStatusText()
  );

  Serial.println(
    "CLOUD: Data published to Adafruit IO."
  );
}


// ============================================================
// SENSOR READING
// ============================================================

void readSensors() {

  // ----------------------------------------------------------
  // DHT22
  // ----------------------------------------------------------

  TempAndHumidity dhtData =
    dht.getTempAndHumidity();

  if (
    isnan(dhtData.temperature) ||
    isnan(dhtData.humidity)
  ) {

    dhtHealthy = false;

    Serial.println(
      "SENSOR WARNING: Invalid DHT22 reading."
    );
  }

  else {

    dhtHealthy = true;

    temperature =
      dhtData.temperature;

    humidity =
      dhtData.humidity;
  }


  // ----------------------------------------------------------
  // PIR MOTION
  // ----------------------------------------------------------

  pirMotion =
    digitalRead(PIR_PIN) == HIGH;

  if (pirMotion) {

    lastMotionTime = millis();

    if (!occupied) {

      Serial.println(
        "OCCUPANCY: Person detected."
      );
    }

    occupied = true;
  }

  else if (
    occupied &&
    millis() - lastMotionTime >=
    OCCUPANCY_TIMEOUT_MS
  ) {

    occupied = false;

    Serial.println(
      "OCCUPANCY: Timeout reached - room unoccupied."
    );
  }


  // ----------------------------------------------------------
  // LIGHT SENSOR
  // ----------------------------------------------------------

  lightRaw =
    analogRead(LDR_PIN);

  /*
     Convert raw ESP32 ADC reading to percentage.

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
// CLIMATE AUTOMATION
// ============================================================

void updateClimateControl() {

  // ----------------------------------------------------------
  // FAILSAFE
  // ----------------------------------------------------------

  // Invalid DHT22 data means climate control is disabled.

  if (!dhtHealthy) {

    climateMode = CLIMATE_OFF;

    digitalWrite(
      CLIMATE_RELAY_PIN,
      LOW
    );

    return;
  }


  // ----------------------------------------------------------
  // OCCUPANCY-AWARE CONTROL
  // ----------------------------------------------------------

  if (!occupied) {

    climateMode = CLIMATE_OFF;
  }


  // ----------------------------------------------------------
  // CLIMATE CURRENTLY OFF
  // ----------------------------------------------------------

  else if (
    climateMode == CLIMATE_OFF
  ) {

    if (
      temperature >= COOLING_ON_TEMP
    ) {

      climateMode =
        CLIMATE_COOLING;

      Serial.println(
        "AUTOMATION: Cooling activated."
      );
    }

    else if (
      temperature <= HEATING_ON_TEMP
    ) {

      climateMode =
        CLIMATE_HEATING;

      Serial.println(
        "AUTOMATION: Heating activated."
      );
    }
  }


  // ----------------------------------------------------------
  // COOLING HYSTERESIS
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
  // HEATING HYSTERESIS
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


  digitalWrite(
    CLIMATE_RELAY_PIN,
    climateMode ==
      CLIMATE_OFF
        ? LOW
        : HIGH
  );
}


// ============================================================
// SMART LIGHTING
// ============================================================

void updateRoomLighting() {

  // No occupant = light always OFF.

  if (!occupied) {

    if (roomLightOn) {

      Serial.println(
        "AUTOMATION: Room light OFF - room unoccupied."
      );
    }

    roomLightOn = false;
  }


  // ----------------------------------------------------------
  // DARK + OCCUPIED
  // ----------------------------------------------------------

  else if (
    !roomLightOn &&
    lightPercent <=
      LIGHT_ON_LEVEL
  ) {

    roomLightOn = true;

    Serial.println(
      "AUTOMATION: Room light ON - dark and occupied."
    );
  }


  // ----------------------------------------------------------
  // BRIGHT ENOUGH
  // ----------------------------------------------------------

  else if (
    roomLightOn &&
    lightPercent >=
      LIGHT_OFF_LEVEL
  ) {

    roomLightOn = false;

    Serial.println(
      "AUTOMATION: Room light OFF - sufficient daylight."
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
// INTELLIGENT BLIND CONTROL
// ============================================================

void updateBlinds() {

  /*
     MULTI-SENSOR EDGE INTELLIGENCE

     Blinds only close when BOTH:

     1. Light is strong
     2. Temperature is warm

     This represents reducing solar heat gain without
     unnecessarily blocking useful daylight.
  */


  // ----------------------------------------------------------
  // CLOSE BLINDS
  // ----------------------------------------------------------

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
      "INTELLIGENCE: Blinds CLOSED - bright and warm."
    );
  }


  // ----------------------------------------------------------
  // OPEN BLINDS
  // ----------------------------------------------------------

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
      "INTELLIGENCE: Blinds OPEN."
    );
  }
}


// ============================================================
// APPLY AUTOMATION RULES
// ============================================================

void applyAutomationRules() {

  updateClimateControl();

  updateRoomLighting();

  updateBlinds();
}


// ============================================================
// SERIAL STATUS DISPLAY
// ============================================================

void printSystemState() {

  Serial.println();

  Serial.println(
    "=================================================="
  );

  Serial.println(
    "       3707ICT SMART HOME SYSTEM STATUS"
  );

  Serial.println(
    "=================================================="
  );

  Serial.print("Temperature       : ");
  Serial.print(temperature, 1);
  Serial.println(" C");

  Serial.print("Humidity          : ");
  Serial.print(humidity, 1);
  Serial.println(" %");

  Serial.print("Ambient Light     : ");
  Serial.print(lightPercent);
  Serial.println(" %");

  Serial.print("Raw LDR           : ");
  Serial.println(lightRaw);

  Serial.print("Motion            : ");

  Serial.println(
    pirMotion
      ? "DETECTED"
      : "CLEAR"
  );

  Serial.print("Occupancy         : ");

  Serial.println(
    occupied
      ? "OCCUPIED"
      : "UNOCCUPIED"
  );

  Serial.print("Room Light        : ");

  Serial.println(
    roomLightOn
      ? "ON"
      : "OFF"
  );

  Serial.print("Climate Mode      : ");

  Serial.println(
    climateModeText()
  );

  Serial.print("Blinds            : ");

  Serial.println(
    blindsClosed
      ? "CLOSED"
      : "OPEN"
  );

  Serial.print("DHT22 Status      : ");

  Serial.println(
    dhtHealthy
      ? "OK"
      : "FAULT"
  );

  Serial.print("Wi-Fi             : ");

  Serial.println(
    WiFi.status() ==
      WL_CONNECTED
        ? "CONNECTED"
        : "DISCONNECTED"
  );

  Serial.print("MQTT              : ");

  Serial.println(
    mqttClient.connected()
      ? "CONNECTED"
      : "OFFLINE"
  );

  Serial.print("System Status     : ");

  Serial.println(
    systemStatusText()
  );

  Serial.print("Adaptive Polling  : ");

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
    "Initialising..."
  );


  // ----------------------------------------------------------
  // INPUTS
  // ----------------------------------------------------------

  pinMode(
    PIR_PIN,
    INPUT
  );

  pinMode(
    LDR_PIN,
    INPUT
  );


  // ----------------------------------------------------------
  // OUTPUTS
  // ----------------------------------------------------------

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


  // ----------------------------------------------------------
  // DHT22
  // ----------------------------------------------------------

  dht.setup(
    DHT_PIN,
    DHTesp::DHT22
  );


  // ----------------------------------------------------------
  // SERVO
  // ----------------------------------------------------------

  blindsServo.setPeriodHertz(50);

  blindsServo.attach(
    BLINDS_SERVO_PIN,
    500,
    2400
  );

  blindsServo.write(0);


  // ----------------------------------------------------------
  // MQTT
  // ----------------------------------------------------------

  mqttClient.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );

  mqttClient.setBufferSize(256);


  // ----------------------------------------------------------
  // WI-FI
  // ----------------------------------------------------------

  maintainWiFi();


  // ----------------------------------------------------------
  // DEMONSTRATION MODE
  // ----------------------------------------------------------

  if (DEMO_MODE) {

    Serial.println(
      "DEMO MODE: Occupancy timeout = 10 seconds."
    );
  }

  else {

    Serial.println(
      "NORMAL MODE: Occupancy timeout = 5 minutes."
    );
  }


  // ----------------------------------------------------------
  // INITIAL SYSTEM STATE
  // ----------------------------------------------------------

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

  // ----------------------------------------------------------
  // NETWORK MAINTENANCE
  // ----------------------------------------------------------

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
  // ADAPTIVE CLOUD PUBLISHING
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
