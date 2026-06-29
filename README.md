# Amigo 👾

**Amigo** is an open-source, smart desktop companion device designed to help you stay focused while adding a touch of personality to your workspace. 

I created it because I want to have a more phone free experience in the morning and in general. Obviously a lame but logical excuse everymorning to have it near you is because you need alarms. Sometimes you actually want to be productive with a phone like setting timers, take brain breaks, and more. When using a phone its easy to fall into a trap of using it too long after only one "brain break". I hope Amigo keeps you on task and makes you less dependent on your phone. 

Housed in a compact 80×80×80mm 3D-printed cube, Amigo features a tactile 3×3 mechanical keyboard, a 2.8" vibrant color display, and a dedicated speaker. Powered by an ESP32-S3 microcontroller, it pairs with your phone over BLE to enforce focus sessions, acts as an animated desktop buddy, and even runs retro mini-games!

---

## ✨ Features

- **Focus Mode Lockout:** Pairs with a mobile companion app over BLE to physically enforce focus sessions and block distractions.
- **Animated Buddy Face:** An idle character with unique personalities to keep you company.
- **Retro Mini-Games:** Play memory games, chess training, and retro classics (Tetris, Snake, Flappy Bird) right on your desk.
- **Smart Photo Display:** Rotates through curated image sets, dynamically refreshed over WiFi via OTA from a remote server.
- **Curated Notifications:** Displays filtered phone notifications so you can stay in the loop without picking up your device.
- **Satisfying Tactile Input:** A 3×3 grid of hot-swappable Kailh Choc V1 mechanical switches for navigating menus and playing games.

---

## 🛠️ Hardware Architecture

Amigo utilizes a two-PCB design optimized for its cubic form factor:
1. **Main Board (50×50mm):** Houses the ESP32-S3, power management, charging circuitry, and display/audio connectors.
2. **Switch Plate (65×65mm):** A dedicated board for the 3×3 mechanical switch matrix, hot-swap sockets, and anti-ghosting diodes. Connected via a ribbon cable.

### Core Components
* **MCU:** ESP32-S3-WROOM-1-N16R8 (16MB flash, 8MB PSRAM)
* **Display:** 2.8" HiLetgo ILI9341 SPI TFT Color LCD (240×320)
* **Audio:** MAX98357A I2S Class-D Amplifier + CQRobot 3W 8Ω Speaker
* **Switches:** 9× Kailh Choc V1 Brown (tactile) with Kailh PG1350 hot-swap sockets
* **Battery & Power:** EEMB 1100mAh LiPo (with built-in PCM and inline 2A PTC polyfuse), TP4056-42 IC for charging via USB-C, AP2112K-3.3 LDO.
* **Enclosure:** Custom 80mm 3D-printed cube (Fusion 360). 

---

## 💻 Software Stack (Planned)

Amigo is built to be easily customizable and updatable remotely. 

* **Framework:** Arduino IDE (ESP32-S3 core) / ESP-IDF
* **Graphics/UI:** TFT_eSPI or Adafruit ILI9341 + Adafruit_GFX
* **Audio:** ESP32-audioI2S for chiptunes and voice prompts
* **Storage:** LittleFS for local image and asset storage (no SD card needed)
* **Connectivity:** ESP32 NimBLE (Bluetooth) & WiFi for backend syncing
* **OTA Updates:** Remote firmware and content updates pulled from a static JSON backend (e.g., GitHub Pages or Cloudflare Workers).

### Multi-Tenant Content
Amigo supports multi-tenant content architecture. By checking in with a server using its MAC address, an Amigo unit can dynamically download different "buddy personalities" or image sets (e.g., a "dev" set for testing vs. a "consumer" set for standard use).

---

## 🚧 Project Status

**Current Phase: End of Phase 1 (Hardware Complete)**

- [x] Schematic & PCB Layout (Main Board & Switch Plate)
- [x] DRC check & Bug Audit
- [x] Gerbers generated and PCBs ordered
- [x] Components & Peripherals sourced
- [x] 3D Enclosure CAD
- [ ] Hardware Assembly
- [ ] Firmware Development
- [ ] Companion Phone App (Android/BLE)
- [ ] Backend Server Setup

---

## ⚠️ Safety Notes for Assembly
* **Battery Polarity:** Always verify the LiPo battery polarity with a multimeter before plugging it into the board! Use the inline 2A polyfuse for added safety.
* **Soldering:** The project utilizes SMD components. Use low-temp lead-free solder paste (e.g., Sn42/Bi58) and a hot air rework station. 
* **Antenna Clearance:** The ESP32-S3 antenna faces the LEFT side wall of the cube. Ensure no metal hardware (screws, heat-set inserts) encroaches on the keep-out zone to maintain WiFi/BLE performance.

---

## 🚀 Roadmap (v2 Considerations)
While v1 focuses on a stable, robust baseline, future iterations may include:
1. Incorporating the inline polyfuse directly onto the PCB.
2. Using M3 heat-set inserts for standardizing enclosure assembly.
3. Exploring larger batteries or touch-capable displays (e.g., GC9A01).

---
*Created by Ethan*
