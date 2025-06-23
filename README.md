# Powermeter to MQTT & Webserver via Modbus using Ethernet or WiFi with OTA

A Firmware-Project for Espressif Microcontrollers (Chips like: **ESP32**, ESP32S3, ESP32H2 ...) that reads **Powermeter** registers and publishes the data to **MQTT**, while also providing a **Weberver** interface for real-time-monitoring and logging.

It works with almost all [Eastron SDM](https://www.eastroneurope.com/products/category/din-rail-mounted-metering)-Powermeter-Device (integrated) and it is configured for the Easton **SDM630**. Only small adjustments needed, to work with a different type, or Modbus-Powermeters from other vendors.

This is purely **based [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html)** (Espressif IoT Development Framework), it is NOT an Arduino-Project.

## Features

- Continuously reads Powermeter's values via **Modbus protocol**.
- Distinguish between **priority- and normal values**. Prioritiy-values are read in shorter cycles.  
- Publishes read values to an **MQTT** broker for integration with IoT platforms.
- Only publish values to MQTT if it has significanlty enough changed. What is significant, is set for register seperatly.
- Provides an embedded *async* **Webserver** (on ESP) for real-time monitoring.
- Provides a **Webserial**-Page to investigate live-loggings on Webpage.
- Establishes **connection to LAN** either with **Ethernet** or **WiFi**.
- Synchronize the local time from **NTP Server**, so data are sent with timestamp.
- Offers Over-The-Air (**OTA**) updates, as Microcontroller might not be connected to USB.
- Microcontrollers can be accessed in your network via a **Hostname** or **IP** (can be fixed IP).
- HTML-Pages are stored in **LittleFS** (File-System) for easy access and modification.
- Consits of **7 components** and 'main'.
- Allmost all component provides `menuconfig` utilization for config. They are designed to be reused in other projects.

## Hardware requirements

- **Microcontroller** Board with any **chip of the ESP32-Family**.
    - <u>Recommended:</u><br> Chips with JTAG integrated like ESP32S2, ESP32S3 or ESP32H2 for easy debugging via USB.<br>Search for 'JTAG' in technical datasheet of the chip, [ESP32S3-Datasheet-Example](https://www.espressif.com/sites/default/files/documentation/esp32-s2_datasheet_en.pdf).

- **Powermeter** with MODBUS-Interface.

- **UART**(Serial)-to-**MOBUS**(RS485)-**Adapter**. 
    - <u>Recommended:</u><br>With calvanic isoltion of MCU from MODBUS.

- Optional: **Ethernet-Shield** (e.g. with W5500-Chip), when you want to connect to LAN via Ethernet.

## Software requirements

- For compile & flashing: [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)-Tools.

<u>Recommended:</u>

- ESP-IDF-Extension in Visual Studio Code as [IDE](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#ide). This includes the ESP-IDF-Tools.

## Getting Started

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

## Overview of components

You find them in the folder: '`components`' and are used be the main-app.

| Component (Folder)| Purpose | `menuconfig`-Title | `#include` |
|:- | :- | :- | :- |
|`xlan_connection`| Get Connection to LAN with Ethernet or WiFI. Do all the network-inerface stuff. | `My xLAN (ETHERNET/WiFi) Config with Hostname`|`"xlan_connection.h"`|
|`NTPSync_and_localTZ`| Get the time from a NTP-Server and set local timezone at MCU. | `My Time Sync Configuration`|`"NTPSync_and_localTZ.h"`|
|`Modbus_UART_RTU`| Config a UART Port, establish the MODBUS interface with suiteable protokoll and provide the Event-Handlers.|`My Modbus UART/Serial RTU Config`|`"Modbus_UART_RTU.h"`|
|`myMQTT`| Start the MQTT-Client and provide the Event-Handlers for the MQTT (in- & out messages). |`My MQTT Config`|`"myMQTT.h"`|
|`async_httpd_helper`| Start suitable amount of workers for the **async** Webserver, that runs as a daemon. |`My async HTTPD Helper (Worker Tasks) Configuration`|`"async_httpd_helper.h"`|
|`OTA_mDNS`|Provides possibility for Over-The-Air updates with utilizing mDNS (Zeroconfig) capability. |`My OTA updates using mDNS-URLs Configuration`|`"OTA_mDNS.h"`|
|`SDM`|*Unused in main-app*, but holding Powermeters Modbus-Register-Info for Powermeters of Eastron SDM-Devices|*-none-*|`"SDM.h"`|

## Scripts in Project-Folder

| Script-Name | Script-Type | Purpose |
|:- | :-: | :- |
|`ota_update/install_py_requirements.sh`| Shell | Install needed python modules for `ota_server-one-shot.py` |
|`ota_server-one-shot.py`| Python | Provides the file for OTA-update at local machine in same LAN for the MCU. It starts a Zeroconfig-server to service. Shut's down automatically after file is delivered once.|
|`provide_ota_update.sh`| Shell | Just for convinience, start the script `ota_server-one-shot.py`|
|`toggle_project_config.sh`| Shell | The VSCode Extension 'ESP-IDF' provides the 'Project Configuration Editor'. This is switched on/of by this srcipt.|
|`clean_all.sh`| Shell | Cleans everything related with 'build' including `skdconfig` and `depencency.lock` leads to a **'virgin'-state.**|

## Other Files in Project-Folder

| Script-Name | Content |
|:- | :- |
|`version.txt`| Hold the Version-Number for firmware, that during 'build' stored in NVS |
|`storage_at_runtime/prm_webserver.html`|  |
|`prm_webserver.html`| Powermeter Webpage for Monitoring of register-values. Update at runtime after each cycle.|
|`log_webserver.html`| Webpage to show log-messages during runtime = WebSerial.|
|`partitions.csv`| Table partion definitions. Needed for 'OTA' and 'LittleFS'|

## License

This project is licensed under the MIT License.

## Acknowledgements

- [ESP-IDF](https://github.com/espressif/esp-idf)

---
