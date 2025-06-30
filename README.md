# Powermeter to MQTT & Webserver via Modbus using Ethernet or WiFi with OTA

A Firmware-Project for Espressif microcontrollers (such as **ESP32**, ESP32S3, ESP32H2 ...) that reads **Powermeter** registers and publishes the data to **MQTT**, while also providing a **Webserver** interface for real-time-monitoring and logging.

It works with almost all [Eastron SDM](https://www.eastroneurope.com/products/category/din-rail-mounted-metering)-Powermeter-Device (integrated) and it is configured for the Eastron **SDM630**. Only small adjustments needed, to work with a different type, or Modbus-Powermeters from other vendors.

This is purely **based on [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html)** (Espressif IoT Development Framework), it is NOT an Arduino-Project.

## ✅ Features

- Continuously reads Powermeter's values via **Modbus RTU**.
- Distinguish between **priority** and **normal** values. Priority-values are read more often.  
- Publishes read values to an **MQTT** broker for integration with IoT platforms.
- Only publishes values to MQTT if they have changed **significantly** (per-register threshold configurable).
- Embedded *async* **Webserver** (on ESP) for real-time monitoring.
- **WebSerial** interface to view live logs in the browser.
- LAN connection via either with **Ethernet** or **WiFi**.
- Synchronize local time using an **NTP Server**, allowing data to be timestamped.
- Supports **Over-The-Air (OTA)** updates, eliminating the need for USB access.
- Devices can be reached by **Hostname** or **IP** (can be fixed IP).
- HTML-Pages are stored in **LittleFS** (File-System) for easy updates and editing.
- Consists of **7 reusable components** plus a main application.
- Almost all components include `menuconfig` configuration options and are designed for reused in other projects.

## 🧰 Hardware Requirements

- **Microcontroller** Board with any **chip of the ESP32-Family**.
    - <u>Recommended:</u><br> Chips with JTAG integrated like ESP32S2, ESP32S3 or ESP32H2 for easy debugging via USB.<br>Search for 'JTAG' in technical datasheet of the chip, [ESP32S3-Datasheet-Example](https://www.espressif.com/sites/default/files/documentation/esp32-s2_datasheet_en.pdf).

- **Powermeter** with a **Modbus**-Interface.

- **UART**(Serial)-to-**MOBUS**(RS485)-**Adapter**. 
    - <u>Recommended:</u><br>With galvanic isolation of MCU from MODBUS.

- Optional: **Ethernet-Shield** (e.g. with W5500-Chip), for to LAN connection via Ethernet.

## 💻 Software Requirements

- For compile and flashing: [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)-Tools.

<u>Recommended:</u>

- ESP-IDF-Extension in Visual Studio Code as [IDE](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#ide), which includes all required tools and project configuration UI.

## 🚀 Getting Started

### Setup

1. Clone this repository:
    ```sh
    git clone https://github.com/yourusername/Powermeter-Regs-2-MQTT-Webserver.git
    cd Powermeter-Regs-2-MQTT-Webserver
    ```

2. Configure the project:
    ```sh
    idf.py menuconfig
    ```
    - Set Wi-Fi credentials, MQTT broker address, and power meter parameters.

3. Build and flash:
    ```sh
    idf.py build
    idf.py -p <PORT> flash
    ```

4. Monitor output:
    ```sh
    idf.py monitor
    ```

## 🧱 Project Components

Located in the '`components/`'directory and used by the main application:

> See the list of components: &nbsp; [COMPONENTS.md](components/COMPONENTS.md)

## 🛠️ Scripts in Project-Folder

| Script-Name | Script-Type | Purpose |
|:- | :-: | :- |
|`ota_update/install_py_requirements.sh`| Shell | Installs Python modules needed for `ota_server-one-shot.py` |
|`ota_server-one-shot.py`| Python | Serves the OTA file over local network using Zeroconf; shuts down after delivery.|
|`provide_ota_update.sh`| Shell | Just for convenience, to start `ota_server-one-shot.py`|
|`toggle_project_config.sh`| Shell | Enables/disables the Project Configuration Editor in VSCode ESP-IDF extension.|
|`clean_all.sh`| Shell | Cleans everything related with 'build' including `skdconfig` and `depencency.lock` leads to a **'virgin'-state.**|

## 📄 Other Files in Project-Folder

| File | Content |
|:- | :- |
|`version.txt`| Holds the firmware version, written to None-Volatile-Storage(NVS) during build.|
|`storage_at_runtime/prm_webserver.html`| Webpage to monitoring the Powermeter register-values. Updated at runtime after each read-cycle.|
|`storage_at_runtime/webserial.html`| Webpage to show log-messages during runtime (WebSerial)|
|`partitions.csv`| Partition layout definition needed for OTA and LittleFS.|

## 📜 License

This project is licensed under the MIT License.

## 🙌 Acknowledgements

- [ESP-IDF](https://github.com/espressif/esp-idf)

---
