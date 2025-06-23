
#ifndef SDM_h
#define SDM_h
/*----------------------------------------------------------------------------------------------
| ESP32 Chip  | Available UARTS         | Notes 
|---------------------------------------|-------------------------------------------------------
| ESP32       | 3 (UART0, UART1, UART2) | All fully functional and configurable via GPIO matrix.
| ESP32-S2    | 2 (UART0, UART1)        | Lacks Bluetooth; fewer UARTs than original ESP32.
| ESP32-S3    | 3 (UART0, UART1, UART2) | Like original ESP32, with USB-OTG support.
| ESP32-C3    | 2 (UART0, UART1)        | RISC-V core, low-cost, fewer peripherals.
| ESP32-C6    | 2 (UART0, UART1)        | Adds Wi-Fi 6 and BLE 5.3, limited UARTs
| ESP32-H2    | 2 (UART0, UART1)        | BLE + IEEE 802.15.4 (Thread/Zigbee), no Wi-Fi.
------------------------------------------------------------------------------------------------*/
//--------------------------------------------------------------------------------------------------------------------------------------
//      REGISTERS LIST FOR SDM DEVICES                                                                                                 |
//--------------------------------------------------------------------------------------------------------------------------------------
//      REGISTER NAME                    REGISTER ADDRESS |  UNIT     | SDM630 | SDM230 | SDM220 | SDM120CT| SDM120 | SDM72D | SDM72 V2|
//--------------------------------------------------------------------------------------------------------------------------------------
#define SDM_PHASE_1_VOLTAGE                        0x0000 //  V       |    1   |   1    |   1    |    1    |   1    |        |    1    |         
#define SDM_PHASE_2_VOLTAGE                        0x0002 //  V       |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_3_VOLTAGE                        0x0004 //  V       |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_1_CURRENT                        0x0006 //  A       |    1   |   1    |   1    |    1    |   1    |        |    1    |
#define SDM_PHASE_2_CURRENT                        0x0008 //  A       |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_3_CURRENT                        0x000A //  A       |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_1_POWER                          0x000C //  W       |    1   |   1    |   1    |    1    |   1    |        |    1    |
#define SDM_PHASE_2_POWER                          0x000E //  W       |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_3_POWER                          0x0010 //  W       |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_1_APPARENT_POWER                 0x0012 //  VA      |    1   |   1    |   1    |    1    |   1    |        |    1    |
#define SDM_PHASE_2_APPARENT_POWER                 0x0014 //  VA      |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_3_APPARENT_POWER                 0x0016 //  VA      |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_1_REACTIVE_POWER                 0x0018 //  VAr     |    1   |   1    |   1    |    1    |   1    |        |    1    |
#define SDM_PHASE_2_REACTIVE_POWER                 0x001A //  VAr     |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_3_REACTIVE_POWER                 0x001C //  VAr     |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_1_POWER_FACTOR                   0x001E //          |    1   |   1    |   1    |    1    |   1    |        |    1    |
#define SDM_PHASE_2_POWER_FACTOR                   0x0020 //          |    1   |        |        |         |        |        |    1    |
#define SDM_PHASE_3_POWER_FACTOR                   0x0022 //          |    1   |        |        |         |        |        |    1    |
#define SDM_AVERAGE_L_TO_N_VOLTS                   0x002A //  V       |    1   |        |        |         |        |        |    1    |
#define SDM_AVERAGE_LINE_CURRENT                   0x002E //  A       |    1   |        |        |         |        |        |    1    |
#define SDM_SUM_LINE_CURRENT                       0x0030 //  A       |    1   |        |        |         |        |        |    1    |
#define SDM_TOTAL_SYSTEM_POWER                     0x0034 //  W       |    1   |        |        |         |        |   1    |    1    |
#define SDM_TOTAL_SYSTEM_APPARENT_POWER            0x0038 //  VA      |    1   |        |        |         |        |        |    1    |
#define SDM_TOTAL_SYSTEM_REACTIVE_POWER            0x003C //  VAr     |    1   |        |        |         |        |        |    1    |
#define SDM_TOTAL_SYSTEM_POWER_FACTOR              0x003E //          |    1   |        |        |         |        |        |    1    |
#define SDM_FREQUENCY                              0x0046 //  Hz      |    1   |   1    |   1    |    1    |   1    |        |    1    |
#define SDM_IMPORT_ACTIVE_ENERGY                   0x0048 //  kWh/MWh |    1   |   1    |   1    |    1    |   1    |   1    |    1    |
#define SDM_EXPORT_ACTIVE_ENERGY                   0x004A //  kWh/MWh |    1   |   1    |   1    |    1    |   1    |   1    |    1    |
#define SDM_LINE_1_TO_LINE_2_VOLTS                 0x00C8 //  V       |    1   |        |        |         |        |        |    1    |
#define SDM_LINE_2_TO_LINE_3_VOLTS                 0x00CA //  V       |    1   |        |        |         |        |        |    1    |
#define SDM_LINE_3_TO_LINE_1_VOLTS                 0x00CC //  V       |    1   |        |        |         |        |        |    1    |
#define SDM_AVERAGE_LINE_TO_LINE_VOLTS             0x00CE //  V       |    1   |        |        |         |        |        |    1    |
#define SDM_NEUTRAL_CURRENT                        0x00E0 //  A       |    1   |        |        |         |        |        |    1    |
#define SDM_TOTAL_ACTIVE_ENERGY                    0x0156 //  kWh     |    1   |   1    |   1    |    1    |   1    |   1    |    1    |
#define SDM_TOTAL_REACTIVE_ENERGY                  0x0158 //  kVArh   |    1   |   1    |   1    |    1    |   1    |        |    1    |
#define SDM_CURRENT_RESETTABLE_TOTAL_ACTIVE_ENERGY 0x0180 //  kWh     |        |   1    |        |         |        |   1    |    1    |
#define SDM_CURRENT_RESETTABLE_IMPORT_ENERGY       0x0184 //  kWh     |        |        |        |         |        |   1    |    1    |
#define SDM_CURRENT_RESETTABLE_EXPORT_ENERGY       0x0186 //  kWh     |        |        |        |         |        |   1    |    1    |
#define SDM_NET_KWH                                0x018C //  kWh     |        |        |        |         |        |        |    1    |
#define SDM_IMPORT_POWER                           0x0500 //  W       |        |        |        |         |        |   1    |    1    |
#define SDM_EXPORT_POWER                           0x0502 //  W       |        |        |        |         |        |   1    |    1    |
//--------------------------------------------------------------------------------------------------------------------------------------
#endif // SDM_h