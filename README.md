# 3707ICT Smart Home IoT Automation System

This project is a fully simulated ESP32 smart-home automation system designed for Wokwi and developed using PlatformIO in Visual Studio Code.

The system monitors temperature, humidity, motion and ambient light. It makes local context-aware automation decisions, controls simulated smart-home devices and securely publishes system data to Adafruit IO using MQTT over TLS.

The system is designed so that **local automation continues even if Wi-Fi or Adafruit IO is unavailable**.

---

## Simulated Components

| Component | Purpose | ESP32 Pin |
| --- | --- | ---: |
| DHT22 | Temperature and humidity sensing | GPIO 15 |
| PIR sensor | Motion and occupancy detection | GPIO 27 |
| Photoresistor / LDR | Ambient light sensing | GPIO 34 |
| Yellow LED | Simulated room lighting | GPIO 2 |
| Relay module | Simulated climate-control power | GPIO 26 |
| Servo | Simulated motorised blinds | GPIO 18 |

The relay represents whether the climate-control system is powered.

The current climate mode is displayed in the Serial Monitor and published to the `climate-mode` Adafruit IO feed as:

* `HEATING`
* `COOLING`
* `OFF`

---

# Running the Project

## PlatformIO and Wokwi

The project supports PlatformIO and the Wokwi VS Code extension.

The repository includes:

* main ESP32 source code
* `secrets.example.h`
* `platformio.ini`
* `wokwi.toml`
* Wokwi circuit configuration

The operational `secrets.h` file is stored locally and excluded from the repository using `.gitignore` to prevent Adafruit IO credentials from being exposed.

To run the project:

1. Install the **PlatformIO** extension in Visual Studio Code.
2. Install the **Wokwi** extension.
3. Open the project folder in VS Code.
4. Select the `esp32dev` PlatformIO environment.
5. Copy `secrets.example.h` and rename the copy to `secrets.h`.
6. Replace the placeholder values in `secrets.h` with your Adafruit IO username and AIO key.
7. Build the project.
8. Run **Wokwi: Start Simulator**.
9. Open the Serial Monitor at **115200 baud**.

The `secrets.h` file should remain local and should not be committed to the GitHub repository.

The project must be built before starting Wokwi so the firmware files referenced by `wokwi.toml` are available.

The simulated ESP32 connects to the Wokwi virtual Wi-Fi network using:

```cpp
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
```

---

# Adafruit IO / Secure MQTT

The ESP32 communicates with Adafruit IO using MQTT over a TLS-encrypted connection.

The MQTT configuration is:

```cpp
const char* MQTT_SERVER = "io.adafruit.com";
const int MQTT_PORT = 8883;
```

The project uses:

```cpp
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);
```

Port `8883` is used for secure MQTT communication.

For the Wokwi demonstration, the TLS client is configured using:

```cpp
wifiClient.setInsecure();
```

This means the MQTT traffic is encrypted using TLS, but the ESP32 does **not validate the Adafruit IO server certificate**.

This is acceptable for the simulated Wokwi demonstration. A production implementation should validate the server certificate using an appropriate trusted CA certificate.

---

# Adafruit IO Credentials

Adafruit IO authentication requires an account username and AIO key.

For security, operational credentials are stored locally in:

```text
secrets.h
```

The `secrets.h` file is excluded from the GitHub repository using `.gitignore`.

A template file is provided in the repository:

```text
secrets.example.h
```

The template contains:

```cpp
#pragma once

#define AIO_USERNAME "YOUR_ADAFRUIT_USERNAME"
#define AIO_KEY "YOUR_ADAFRUIT_IO_KEY"
```

To configure the project, copy `secrets.example.h`, rename the copy to `secrets.h`, and replace the placeholder values with your own Adafruit IO credentials.

The main application accesses the credentials using:

```cpp
#include "secrets.h"
```

The operational `secrets.h` file should remain local and should not be committed to the GitHub repository. This prevents the real Adafruit IO key from being exposed while still providing the required credential structure through `secrets.example.h`.

---

# Adafruit IO Feeds

The following Adafruit IO feeds are used. The feed keys must match exactly:

| Feed Key | Data |
| --- | --- |
| `temperature` | Temperature in °C |
| `humidity` | Relative humidity |
| `ambient-light` | Ambient light level from 0–100% |
| `occupancy` | `1` when occupied, `0` when unoccupied |
| `room-light` | `ON` or `OFF` |
| `climate-mode` | `HEATING`, `COOLING` or `OFF` |
| `blinds-position` | `OPEN` or `CLOSED` |
| `system-status` | Overall system/network status |

The Adafruit IO dashboard can use gauges and graphs for environmental sensor data and indicators or text blocks for occupancy, actuator states and system status.

---

# Automation Rules

## 1. Occupancy Detection

The PIR sensor detects motion and marks the room as occupied.

Each motion event resets the occupancy timer.

When no motion has been detected for the configured timeout period, the room becomes unoccupied.

When the room becomes unoccupied:

* Room lighting turns off.
* Climate control turns off.

This prevents lighting and climate equipment from continuing to operate unnecessarily when the room is empty.

---

## 2. Smart Climate Control

Climate control only operates while the room is occupied.

### Cooling

Cooling activates when:

```text
Occupied AND Temperature >= 30°C
```

Cooling remains active until:

```text
Temperature <= 28°C
```

### Heating

Heating activates when:

```text
Occupied AND Temperature <= 16°C
```

Heating remains active until:

```text
Temperature >= 18°C
```

The separate ON and OFF thresholds implement **hysteresis**, preventing rapid switching when the temperature fluctuates around a single threshold.

If the DHT22 returns invalid temperature or humidity data, climate control is automatically disabled as a failsafe.

---

## 3. Occupancy-Aware Smart Lighting

The room light only operates while the room is occupied.

The light turns on when:

```text
Occupied AND Ambient Light <= 35%
```

The light turns off when:

```text
Ambient Light >= 45%
```

The light also turns off immediately when the room becomes unoccupied.

The separate 35% and 45% thresholds provide lighting hysteresis and prevent repeated switching when the light level is close to the switching threshold.

---

## 4. Intelligent Blind Control

The motorised blinds use both temperature and ambient-light measurements.

The blinds close when:

```text
Ambient Light >= 70%
AND
Temperature >= 27°C
```

The blinds reopen when either:

```text
Ambient Light <= 50%
OR
Temperature <= 25°C
```

This provides a **multi-sensor edge intelligence rule**.

Instead of responding to only one sensor, the ESP32 combines environmental conditions to make a local decision. The system attempts to reduce solar heat gain when the room is both bright and warm while avoiding unnecessarily blocking useful daylight.

---

# Adaptive Sensor Polling

The system changes its sensor polling frequency depending on current conditions.

## Fast Polling — 2 seconds

Fast sensor polling is used when:

* The room is occupied.
* Temperature is at or above 30°C.
* Temperature is at or below 16°C.
* Ambient light is at or below 35%.
* Ambient light is at or above 70%.

## Normal Polling — 5 seconds

Normal polling is used when the room is unoccupied and environmental conditions are normal.

This allows the ESP32 to respond more quickly when conditions require attention while reducing unnecessary processing during normal operation.

---

# Adaptive Cloud Publishing

Cloud publishing is deliberately slower than local sensor polling.

## Active or Abnormal Conditions

Adafruit IO is updated every:

```text
20 seconds
```

## Normal Conditions

Adafruit IO is updated every:

```text
60 seconds
```

Local automation does **not** depend on the cloud publishing interval.

The ESP32 continues reading sensors and applying automation rules locally regardless of whether new data is currently being uploaded to Adafruit IO.

---

# Offline Operation

The system is designed so that cloud connectivity is not required for local automation.

If Wi-Fi or MQTT becomes unavailable, the ESP32 continues:

* Reading sensors.
* Detecting occupancy.
* Controlling room lighting.
* Controlling climate operation.
* Controlling the motorised blinds.
* Applying hysteresis and local automation rules.

Only remote Adafruit IO monitoring is affected.

The system can report the following states:

| Status | Meaning |
| --- | --- |
| `ONLINE` | Wi-Fi and MQTT are connected |
| `CLOUD OFFLINE` | Wi-Fi connected but MQTT unavailable |
| `LOCAL MODE` | Wi-Fi unavailable |
| `SENSOR FAULT` | Invalid DHT22 temperature/humidity reading |

Wi-Fi connection attempts are retried every **10 seconds**.

MQTT connection attempts are retried every **5 seconds**.

---

# Demonstration Mode

The current code uses:

```cpp
const bool DEMO_MODE = true;
```

This sets the occupancy timeout to:

```text
10 seconds
```

The shorter timeout makes it practical to demonstrate occupancy behaviour during the project demonstration video.

For normal operation, change:

```cpp
const bool DEMO_MODE = true;
```

to:

```cpp
const bool DEMO_MODE = false;
```

The normal occupancy timeout is then:

```text
300000 ms = 5 minutes
```

---

# Suggested Demonstration Sequence

The demonstration video can show the major operating states and automation rules of the system.

| Test | Simulated Inputs | Expected Result |
| --- | --- | --- |
| Normal unoccupied room | 24°C, no motion, normal light | Climate and room light remain off |
| Unoccupied hot room | 32°C, no motion | Climate remains off |
| Occupied hot room | Motion + 32°C | Relay ON and mode `COOLING` |
| Cooling hysteresis | Reduce temperature to 29°C | Cooling remains on |
| Cooling off | Reduce temperature to 28°C | Climate turns off |
| Occupied cold room | Motion + 15°C | Relay ON and mode `HEATING` |
| Heating hysteresis | Raise temperature to 17°C | Heating remains on |
| Heating off | Raise temperature to 18°C | Climate turns off |
| Occupied dark room | Motion + light ≤35% | Yellow LED turns on |
| Lighting hysteresis | Raise light to 40% | Light remains on |
| Bright room | Raise light to ≥45% | Yellow LED turns off |
| Warm and bright | ≥27°C + ≥70% light | Servo closes blinds |
| Blind hysteresis | Reduce light but keep above 50% | Blinds remain closed |
| Blind reopening | Light ≤50% or temperature ≤25°C | Servo opens blinds |
| Occupancy timeout | No motion for 10 seconds in demo mode | Room becomes unoccupied; light and climate turn off |
| MQTT unavailable | Disconnect or prevent MQTT connection | Local automation continues |
| DHT22 fault | Invalid DHT22 reading | Climate disabled and status shows `SENSOR FAULT` |

The demonstration should also show the Adafruit IO dashboard receiving live data over MQTT TLS and the Serial Monitor reporting the current system state.

---

# Serial Monitor

The Serial Monitor displays:

* Temperature
* Humidity
* Ambient light percentage
* Raw LDR reading
* PIR motion state
* Occupancy state
* Room-light state
* Climate mode
* Blind position
* DHT22 health
* Wi-Fi connection state
* MQTT TLS connection state
* MQTT port
* Overall system status
* Adaptive polling mode

Example:

```text
==================================================
       3707ICT SMART HOME SYSTEM STATUS
==================================================
Temperature       : 24.0 C
Humidity          : 50.0 %
Ambient Light     : 60 %
Raw LDR           : 1638
Motion            : CLEAR
Occupancy         : UNOCCUPIED
Room Light        : OFF
Climate Mode      : OFF
Blinds            : OPEN
DHT22 Status      : OK
Wi-Fi             : CONNECTED
MQTT TLS          : CONNECTED
MQTT Port         : 8883
System Status     : ONLINE
Adaptive Polling  : NORMAL
==================================================
```

---

# Security

The project includes several security considerations.

## Credential Protection

Adafruit IO credentials are stored locally in `secrets.h` rather than being hard-coded directly into the main application source.

The `secrets.h` file is excluded from Git version control using `.gitignore`, preventing operational Adafruit IO credentials from being included in the current version of the repository.

A `secrets.example.h` file containing placeholder values is included in the repository to demonstrate the required credential configuration without exposing real authentication information.

## MQTT Transport Security

Adafruit IO communication uses secure MQTT on:

```text
TCP port 8883
```

The connection is created using `WiFiClientSecure`, providing TLS encryption between the ESP32 and Adafruit IO.

For the Wokwi demonstration:

```cpp
wifiClient.setInsecure();
```

is used, meaning the TLS connection is encrypted but the server certificate is not validated.

A production implementation should validate the Adafruit IO server certificate rather than disabling certificate verification.

## Local Resilience

Automation logic runs locally on the ESP32 rather than relying on the cloud.

Loss of Wi-Fi or MQTT therefore does not prevent the core smart-home automation functions from operating.

---

# System Summary

The project demonstrates an ESP32-based smart-home IoT system with:

* Three sensor inputs.
* Three simulated actuators.
* Local edge automation.
* Occupancy-aware control.
* Temperature hysteresis.
* Lighting hysteresis.
* Multi-sensor intelligent blind control.
* Adaptive sensor polling.
* Adaptive cloud publishing.
* Wi-Fi connectivity.
* MQTT over TLS using port 8883.
* Adafruit IO dashboard integration.
* Secure credential separation using a Git-ignored `secrets.h` and `secrets.example.h`.
* Sensor-fault handling.
* Automatic network reconnection.
* Offline/local operation.

The ESP32 performs automation decisions locally at the edge, while Adafruit IO provides remote monitoring, historical data and dashboard visualisation.

This architecture allows the system to remain operational when cloud connectivity is unavailable while still providing secure remote IoT monitoring when the network connection is available.
