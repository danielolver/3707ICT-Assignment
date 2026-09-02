# 3707ICT Smart Home IoT Automation System

This is a fully simulated ESP32 smart-home project for Wokwi. It monitors
temperature, humidity, motion and ambient light, makes local context-aware
decisions, controls three simulated outputs and publishes its data
to Adafruit IO using MQTT.

## Simulated components

| Component | Wokwi purpose | ESP32 pin |
|---|---|---:|
| DHT22 | Temperature and humidity | GPIO 15 |
| PIR sensor | Motion and occupancy | GPIO 27 |
| Photoresistor module | Ambient light | GPIO 34 |
| Yellow LED | Room lighting | GPIO 2 |
| Relay module | Climate-control power | GPIO 26 |
| Servo | Motorised blinds | GPIO 18 |

The relay represents whether climate equipment is powered. The Serial Monitor
and `climate-mode` Adafruit feed identify whether the current mode is
`HEATING`, `COOLING` or `OFF`.

## Run in Wokwi

1. Create a new **ESP32** project at Wokwi.
2. Replace its `sketch.ino` and `diagram.json` with the supplied files.
3. Add the libraries listed in `libraries.txt` through Library Manager.
4. Start the simulation and open the Serial Monitor at 115200 baud.
5. Click each sensor while the simulation is running to change its values.

### Wokwi in VS Code

The project also includes `platformio.ini` and `wokwi.toml`. Install the
PlatformIO and Wokwi extensions, open this project folder, select the
`esp32dev` environment, build the project and then choose **Wokwi: Start
Simulator**. Building first creates the firmware files referenced by
`wokwi.toml`.

The automation works without Adafruit IO. Leave the credential placeholders
unchanged until local testing is complete.

## Adafruit IO setup

Create these feeds. The keys must match exactly:

| Feed key | Suggested dashboard block |
|---|---|
| `temperature` | Gauge + line chart |
| `humidity` | Gauge + line chart |
| `occupancy` | Indicator |
| `ambient-light` | Gauge + line chart |
| `room-light` | Text or indicator |
| `climate-mode` | Text |
| `blinds-position` | Text |
| `system-status` | Text or indicator |

Then edit these two values near the top of `sketch.ino`:

```cpp
const char* AIO_USERNAME = "YOUR_ADAFRUIT_USERNAME";
const char* AIO_KEY = "YOUR_ADAFRUIT_IO_KEY";
```

Do not include your real Adafruit IO key in screenshots, reports or a public
Git repository.

## Implemented rules

1. Temperature at or above 30 °C **and occupied** → cooling on.
2. Cooling remains on until temperature falls to 28 °C; this is hysteresis.
3. Temperature at or below 16 °C **and occupied** → heating on.
4. Motion/occupancy **and darkness** → room light on.
5. No motion for five minutes → room light and climate control off.
6. Bright **and warm** conditions → blinds close; lower light or temperature
   reopens them.

The lighting thresholds also use hysteresis: on below 35% light and off above
45%. This prevents repeated switching close to one threshold.

## Demonstration mode

The submitted configuration uses the required five-minute occupancy timeout.
For a faster demonstration, temporarily change:

```cpp
const bool DEMO_MODE = false;
```

to `true`. The timeout becomes ten seconds. Return it to `false` for the final
submission unless the marker specifically wants the short demonstration.

## Suggested test sequence

| Test | Simulated inputs | Expected output |
|---|---|---|
| Unoccupied hot room | 32 °C; no motion | Climate remains off |
| Occupied hot room | Motion; 32 °C | Relay on; mode `COOLING` |
| Cooling hysteresis | Lower from 30 °C to 29 °C | Cooling stays on |
| Cooling off | Lower to 28 °C | Relay off |
| Occupied dark room | Motion; light below 35% | Yellow LED on |
| Bright room | Raise light above 45% | Yellow LED off |
| Warm and bright | At least 27 °C and 70% light | Servo closes blinds |
| No motion timeout | Enable demo mode and wait 10 s | Light and climate off |
| Cloud unavailable | Use credential placeholders | Local rules still operate |

