# 🔒 ESP32 Smart Home Automation Project

## 📜 Project Overview

This project is a secure, Wi-Fi-enabled **Smart Home Automation System** built using the **ESP32** microcontroller. It allows real-time control and monitoring of devices (Relays, NeoPixel RGB LEDs, Stepper Motor, DHT22 sensor) through a dynamic **WebSocket** interface served from the ESP32’s file system (LittleFS).

Only authenticated users can access the control panel using a **challenge-response password hashing mechanism** without storing the password in the ESP32 memory — ensuring secure, stateless authentication.

---

## 📌 Features

- 🔐 Secure Challenge-Response WebSocket Authentication
- 🌐 Dynamic web interface with WebSocket-based real-time updates
- 🔌 4x Relays: Toggle control via web and physical buttons
- 🌡️ DHT22: Real-time temperature and humidity monitoring
- 🎨 NeoPixel RGB LED: Color control from UI
- 🤀 Stepper Motor: Angular control via slider with auto-reset on boot
- 📲 Works over local Wi-Fi or fallback hotspot
- 💾 Uses LittleFS for HTML, CSS, JS
- 🎯 UUID-based device recognition (up to 5 remembered clients)
- 📉 System info console and log display

---

## 📂 Folder Structure

```
SmartHome-ESP32/
│
├── data/                      # Web UI files for LittleFS
│   ├── index.html             # Login page
│   ├── control.html           # Smart control panel (after auth)
│   ├── style.css              # Web styling
│   └── script.js              # WebSocket + UI logic
│
├── src/
│   └── main.cpp               # Core firmware code
│
├── platformio.ini             # PlatformIO config
├── README.md                  # Project documentation (this file)
└── credentials_template.h     # Wi-Fi credentials (excluded from repo)
```

---

## 🧠 Authentication Logic (Password Verification)

We use **challenge-response SHA-256 hashing**. Password is never stored or transmitted directly. Here's a highlight from `main.cpp`:

```cpp
#include <SHA256.h>
#include <Preferences.h>

Preferences preferences;
SHA256 sha256;

// Example password hash (not plain password)
const char* correctHash = "5e884898da28047151d0e56f8dc6292773603d0d6aabbdd..."; // sha256("password")

bool verifyPassword(String userResponse, String challenge) {
    String combined = challenge + "password";  // 'password' is never stored; just shown here for understanding
    sha256.reset();
    sha256.update(combined.c_str(), combined.length());
    uint8_t* result = sha256.result();

    String calculatedHash = "";
    for (int i = 0; i < 32; i++) {
        char hex[3];
        sprintf(hex, "%02x", result[i]);
        calculatedHash += hex;
    }

    return userResponse.equals(calculatedHash);
}
```

- Server sends a `challenge` string to the browser.
- Browser JavaScript hashes `challenge + password` and sends it back.
- ESP32 verifies by recomputing SHA-256 on same.

---

## 🔧 Hardware Setup

- ✅ **ESP32 Devkit V1**
- ✅ **4x Relays** on GPIO 16–19
- ✅ **4x Buttons** on GPIO 21–25 (optional debounce)
- ✅ **DHT22 Sensor** on GPIO 4
- ✅ **NeoPixel RGB LED** on GPIO 5 (WS2812)
- ✅ **Stepper Motor** (28BYJ-48 + ULN2003) on GPIOs 26–29
- ✅ Power supply: 5V regulated for relays/motor

---

## 🌍 Web Interface Walkthrough

> *See screenshots below for better understanding (to be added later)*

### 1. **Login Page (**``**)**

- User connects to ESP hotspot `SmartHome-ESP32` (PW: `00000000`) or home Wi-Fi.
- A random challenge string is shown.
- User enters password → client hashes password + challenge → sends hash.



---

### 2. **Main Control Panel (**``**)**

**Top Console:**

- Device info: RAM, CPU freq, uptime, IP
- WebSocket client ID
- Scrollable serial monitor logs

**Relay Control:**

- Toggle buttons for 4 relays\
  *Example log:*
  ```json
  { "Type": "Control", "Key": "Light-1", "Value": "on" }
  ```

**DHT22 Sensor Display:**

- Live temperature and humidity updates every 2s\
  *Example: **`Temp: 29.4°C | Humidity: 62%`*

**NeoPixel Control:**

- HTML color picker to choose RGB values\
  *Logs as:*
  ```json
  { "Type": "Control", "Key": "Ambience", "Value": "#ff8800" }
  ```

**Stepper Motor Slider:**

- Angular control via slider (0°–180°)
- Resets to 0° on boot



---

## 🔐 UUID-Based Authorization

- On successful login, ESP stores `{ username : UUID }` in NVS Preferences
- Maintains last 5 UUIDs
- If a known UUID connects, skips login and serves `control.html` directly

**Code snippet:**

```cpp
preferences.begin("uuid-auth", false);
preferences.putString("user1", "UUID-1234");
String uuid = preferences.getString("user1");
preferences.end();
```

---

## 🧪 Testing Instructions

### First Time Setup

1. Power on ESP32, connect to hotspot `SmartHome-ESP32` (PW: `00000000`)
2. Open `192.168.4.1`
3. Enter password (e.g. `password123`)
4. Control panel opens after successful hash match

### Switching to Home Wi-Fi

1. From UI, go to "Update Credentials"
2. Enter your home Wi-Fi SSID and password
3. Restart ESP32 — now it auto-connects

---

## 🛠️ PlatformIO Configuration

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
build_flags =
  -DCORE_DEBUG_LEVEL=3
lib_deps =
  bblanchon/ArduinoJson
  me-no-dev/ESP Async WebServer
  me-no-dev/AsyncTCP
```

---

## 📓 Future Improvements

- Add OTA firmware updates
- Role-based access (Admin vs Guest)
- MQTT integration with external broker
- Voice assistant control (Alexa/Google)

---

## 🖼️ HardWare Demonstrative
![insides](https://github.com/user-attachments/assets/25806eba-a187-46c8-b7af-26d04db0b16d)    ![front pink](https://github.com/user-attachments/assets/2bbf59df-765a-4559-9201-a1ed88a10e51)
![isometric](https://github.com/user-attachments/assets/01c39c3e-04be-4990-bc4b-aff3959ef20a)    ![ariel inside](https://github.com/user-attachments/assets/ecdc85b9-e509-47de-a7fc-e0726bf10853)
![project backpanel](https://github.com/user-attachments/assets/e3042c15-a66c-48fd-bac3-08a880987ad0)


---
 
## 🖼️ Screenshots & UI
![loginpage](https://github.com/user-attachments/assets/a7f9b7aa-1600-48d5-aa84-54ab88adb05b)    ![controls](https://github.com/user-attachments/assets/7756dab0-7809-4d68-aa91-0fc8d4887f04)
![Debugmode](https://github.com/user-attachments/assets/c522e4aa-e7bc-4530-806e-bfc878e36452)    ![updatespage](https://github.com/user-attachments/assets/9379d6c3-109f-4379-af12-8db8861f7dcc)

---

## 🖼️ Code & Algorithms
![Challenge Generation & UUID MGMT](https://github.com/user-attachments/assets/3204cf70-2b4b-4bba-806f-9341b676ef2d)
![Hash Verification](https://github.com/user-attachments/assets/0400a646-f59a-4a1c-8357-2a1bfae89edb)
![Authentication ](https://github.com/user-attachments/assets/a6631495-37f8-41bd-8f91-70224795246b)
![WebSocket Event Handling](https://github.com/user-attachments/assets/ae9d14e6-f10b-4315-b945-f860c958300a)
![Device Info MGMT](https://github.com/user-attachments/assets/1131aad1-cd75-4d02-a6a7-c5f9ea8bf54b)


## 📣 Credits

**Developer:** Mayur Borgude\
**Year:** 2025\
**Project Type:** B.E. Final Year Embedded + Web IoT System

---

## 📜 License

This project is open-source and free to use under the MIT License. Please give credit where due.

