# 3707ICT Smart Home IoT Automation System

This project is a fully simulated ESP32 smart-home automation system designed for Wokwi.

The system monitors temperature, humidity, motion and ambient light. It makes local context-aware automation decisions, controls simulated smart-home devices and publishes system data to Adafruit IO using MQTT.

The system is designed so that **local automation continues even if Wi-Fi or Adafruit IO is unavailable**.

---

## Simulated Components

| Component           | Purpose                          | ESP32 Pin |
| ------------------- | -------------------------------- | --------: |
| DHT22               | Temperature and humidity sensing |   GPIO 15 |
| PIR sensor          | Motion and occupancy detection   |   GPIO 27 |
| Photoresistor / LDR | Ambient light sensing            |   GPIO 34 |
| Yellow LED          | Simulated room lighting          |    GPIO 2 |
| Relay module        | Simulated climate-control power  |   GPIO 26 |
| Servo               | Simulated motorised blinds       |   GPIO 18 |

The relay represents whether the climate-control system is powered.

The current climate mode is displayed in the Serial Monitor and published to the `climate-mode` Adafruit IO feed as:

* `HEATING`
* `COOLING`
* `OFF`

---

## Running the Project in Wokwi

1. Create or open an **ESP32** project in Wokwi.
2. Add the supplied `sketch.ino` and `diagram.json` files.
3. Add the required libraries from `libraries.txt`.
4. Start the simulation.
5. Open the Serial Monitor at **115200 baud**.
6. Change the simulated sensor values to test the automation rules.

The system can be tested locally without Adafruit IO credentials.

---

## Running Wokwi in VS Code

The project also supports PlatformIO and the Wokwi VS Code extension.

The project includes:

* `platformio.ini`
* `wokwi.toml`

To run the project:

1. Install the **PlatformIO** extension.
2. Install the **Wokwi** extension.
3. Open the project folder in VS Code.
4. Select the `esp32dev` PlatformIO environment.
5. Build the project.
6. Run **Wokwi: Start Simulator**.

The project must be built before starting Wokwi so that the firmware files referenced by `wokwi.toml` exist.

---

## Adafruit IO / MQTT

The ESP32 connects to Adafruit IO using MQTT over Wi-Fi.

The current configuration uses:

```cpp
const char* MQTT_SERVER = "io.adafruit.com";
const int MQTT_PORT = 1883;
```

The Wokwi Wi-Fi network is:

```cpp
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
```

Before connecting to Adafruit IO, set your username and key:

```cpp
const char* AIO_USERNAME = "YOUR_ADAFRUIT_USERNAME";
const char* AIO_KEY = "YOUR_ADAFRUIT_IO_KEY";
```

**Do not commit your real Adafruit IO key to GitHub or include it in the final report.**

---

## Adafruit IO Feeds

Create the following feeds. The feed keys must match exactly:

| Feed Key          | Data                                   |
| ----------------- | -------------------------------------- |
| `temperature`     | Temperature in °C                      |
| `humidity`        | Relative humidity                      |
| `ambient-light`   | Light level from 0–100%                |
| `occupancy`       | `1` when occupied, `0` when unoccupied |
| `room-light`      | `ON` or `OFF`                          |
| `climate-mode`    | `HEATING`, `COOLING` or `OFF`          |
| `blinds-position` | `OPEN` or `CLOSED`                     |
| `system-status`   | Overall system/network status          |

Suggested dashboard blocks include gauges and line charts for environmental sensor data, indicators for occupancy, and text blocks for actuator/system states.

---

# Automation Rules

## 1. Occupancy Detection

The PIR sensor detects motion and marks the room as occupied.

Each motion event resets the occupancy timer.

When no motion has been detected for the configured timeout period, the room becomes unoccupied.

When the room becomes unoccupied:

* Room lighting turns off.
* Climate control turns off.

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

These separate ON and OFF thresholds implement **hysteresis**, preventing the climate system from rapidly switching on and off around a single temperature.

If the DHT22 returns invalid data, climate control is automatically disabled as a failsafe.

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

It also turns off immediately when the room becomes unoccupied.

The separate 35% and 45% thresholds provide lighting hysteresis and prevent repeated switching when the light level is close to a threshold.

---

## 4. Intelligent Blind Control

The motorised blinds use both temperature and ambient-light data.

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

This is a **multi-sensor edge intelligence rule**.

The system attempts to reduce solar heat gain when conditions are both bright and warm, while avoiding unnecessarily blocking useful daylight.

---

# Adaptive Polling

The system changes its sensor polling frequency depending on current conditions.

### Fast Polling — 2 seconds

Fast polling is used when:

* The room is occupied, or
* Temperature is at or above 30°C, or
* Temperature is at or below 16°C, or
* Ambient light is at or below 35%, or
* Ambient light is at or above 70%.

### Normal Polling — 5 seconds

Normal polling is used when the room is unoccupied and environmental conditions are normal.

This allows the system to react more quickly when something important is happening while reducing unnecessary processing during normal conditions.

---

# Adaptive Cloud Publishing

Cloud publishing is deliberately slower than local sensor polling.

### Active / Abnormal Conditions

Adafruit IO is updated every:

```text
20 seconds
```

### Normal Conditions

Adafruit IO is updated every:

```text
60 seconds
```

Local automation does **not** depend on the cloud publishing interval.

---

# Offline Operation

The system continues operating locally if Wi-Fi or MQTT becomes unavailable.

The system status can report:

| Status          | Meaning                                    |
| --------------- | ------------------------------------------ |
| `ONLINE`        | Wi-Fi and MQTT connected                   |
| `CLOUD OFFLINE` | Wi-Fi connected but MQTT unavailable       |
| `LOCAL MODE`    | Wi-Fi unavailable                          |
| `SENSOR FAULT`  | Invalid DHT22 temperature/humidity reading |

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

This makes it practical to demonstrate the occupancy timeout during testing or presentation.

For the full five-minute occupancy timeout, change:

```cpp
const bool DEMO_MODE = true;
```

to:

```cpp
const bool DEMO_MODE = false;
```

The normal timeout is then:

```text
300000 ms = 5 minutes
```

---

# Suggested Test Sequence

| Test                   | Simulated Inputs                         | Expected Result                                     |
| ---------------------- | ---------------------------------------- | --------------------------------------------------- |
| Normal unoccupied room | 24°C, no motion, normal light            | Climate and room light remain off                   |
| Unoccupied hot room    | 32°C, no motion                          | Climate remains off                                 |
| Occupied hot room      | Motion + 32°C                            | Relay ON and mode `COOLING`                         |
| Cooling hysteresis     | Reduce temperature to 29°C               | Cooling remains on                                  |
| Cooling off            | Reduce temperature to 28°C               | Climate turns off                                   |
| Occupied cold room     | Motion + 15°C                            | Relay ON and mode `HEATING`                         |
| Heating hysteresis     | Raise temperature to 17°C                | Heating remains on                                  |
| Heating off            | Raise temperature to 18°C                | Climate turns off                                   |
| Occupied dark room     | Motion + light ≤35%                      | Yellow LED turns on                                 |
| Lighting hysteresis    | Raise light to 40%                       | Light remains on                                    |
| Bright room            | Raise light to ≥45%                      | Yellow LED turns off                                |
| Warm and bright        | ≥27°C + ≥70% light                       | Servo closes blinds                                 |
| Blind hysteresis       | Reduce light but keep above 50%          | Blinds remain closed                                |
| Blind reopening        | Light ≤50% or temperature ≤25°C          | Servo opens blinds                                  |
| Occupancy timeout      | No motion for 10 seconds in demo mode    | Room becomes unoccupied; light and climate turn off |
| MQTT unavailable       | Invalid/placeholder Adafruit credentials | Local automation continues                          |
| DHT22 fault            | Invalid DHT22 reading                    | Climate disabled and status shows `SENSOR FAULT`    |

---

# Serial Monitor

The Serial Monitor displays the current system state, including:

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
* Wi-Fi status
* MQTT status
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
Motion            : CLEAR
Occupancy         : UNOCCUPIED
Room Light        : OFF
Climate Mode      : OFF
Blinds            : OPEN
DHT22 Status      : OK
Wi-Fi             : CONNECTED
MQTT              : CONNECTED
System Status     : ONLINE
Adaptive Polling  : NORMAL
==================================================
```

---

## System Summary

The project demonstrates an ESP32-based smart-home IoT system with:

* Three sensor inputs
* Three simulated actuators
* Local automation rules
* Occupancy-aware control
* Temperature hysteresis
* Lighting hysteresis
* Multi-sensor intelligent blind control
* Adaptive sensor polling
* Adaptive cloud publishing
* Wi-Fi connectivity
* MQTT communication
* Adafruit IO dashboard integration
* Sensor-fault handling
* Offline/local operation

The ESP32 performs automation decisions locally at the edge, while Adafruit IO provides remote monitoring and visualisation.
