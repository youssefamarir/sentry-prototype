# SENTRY (prototype)

SENTRY is an environmental and network monitor for network closets (IDF/MDF rooms). These rooms hold the switches a building depends on, and they are usually hot, closed, and unwatched. SENTRY puts a small sensor node in the rack and reports to a central hub, so a technician sees a problem before the equipment fails.

This is the original prototype, which I designed and built on my own. A full version is in development.

## How it works

- **Node:** an ESP32-S3 board with a BME688 sensor, a round touchscreen, Ethernet, and Wi-Fi. It reads temperature, humidity, pressure, and air quality, shows them on the screen, and works out which switch and port it is plugged into by reading LLDP, CDP, and FDP frames.
- **Hub:** a Raspberry Pi running an MQTT broker. It collects data from every node, stores it, and shows all closets on a touchscreen and a web dashboard.
- **Link:** each node publishes JSON over MQTT every 3 seconds. A retained Last Will message tells the hub when a node drops off.

Nodes keep working on their own if the hub is down. The hub only watches; it never controls a node.

## Repo layout

| Folder | What it is | Status |
|---|---|---|
| `node/` | ESP32-S3 firmware in C (ESP-IDF, PlatformIO, LVGL) | In the repo |
| `hub/` | Raspberry Pi hub in Python (Mosquitto, paho-mqtt, SQLite, PyQt5, Flask) | Coming soon |

Build and wiring instructions for the node are in [node/README.md](node/README.md).

## What I did

Everything in this prototype is my own work:

- Chose the hardware: ESP32-S3 board, BME688 sensor, display, and Raspberry Pi
- Wired and brought up the node
- Wrote the node firmware and touchscreen interface
- Set up the Raspberry Pi and wrote the hub software

## Prototype notes

Wi-Fi, broker address, and API key are read from a local `secrets.h` file that is not in the repo. See `node/src/secrets.example.h`.
