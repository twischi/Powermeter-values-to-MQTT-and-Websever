## 🧱 Project Components List

Located in the '`components/`'directory and used by the main application:

| Component (Folder)| Purpose | `menuconfig`-Title | `#include` |
|:- | :- | :- | :- |
|`xlan_connection`| Manages LAN connection via Ethernet or WiFi. Includes all networking setup. | `My xLAN (ETHERNET/WiFi) Config with Hostname`|`"xlan_connection.h"`|
|`NTPSync_and_localTZ`| Synchronizes time via NTP and sets local timezone on the MCU.| `My Time Sync Configuration`|`"NTPSync_and_localTZ.h"`|
|`Modbus_UART_RTU`| Configures UART for Modbus, manages protocol and event handlers.|`My Modbus UART/Serial RTU Config`|`"Modbus_UART_RTU.h"`|
|`myMQTT`| Initializes MQTT client and handles incoming/outgoing MQTT messages.|`My MQTT Config`|`"myMQTT.h"`|
|`async_httpd_helper`| Starts worker tasks for the **async Webserver** daemon. |`My async HTTPD Helper (Worker Tasks) Configuration`|`"async_httpd_helper.h"`|
|`OTA_mDNS`| Enables OTA updates using mDNS/Zeroconf discovery.|`My OTA updates using mDNS-URLs Configuration`|`"OTA_mDNS.h"`|
|`SDM`|(Unused in main-app) Holds register map definitions for Eastron SDM powermeters.|_(none)_|`"SDM.h"`|