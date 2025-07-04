/*===========================================================================================
 * @file        main.c
 * @author      Thomas Wisniewski
 * @date        2025-06-20
 * @brief       APP to read (Modbus) and publish (MQTT) electrical measurements of SMD devices
 *
 * 
 * @menuconfig  (@main) NO, (@includes) YES 
 * 
 * What does the App?
 *    This main-file is the main entry point for the ESP32 application to
 *    >> Establish a connection to a LAN using Ethernet or WiFi
 *    >> Synchronize the local time with an NTP server
 *    >> Start a WebServer to display the measured values
 *    >> Hold the needed two Webpages with LittleFS for easy access/editing
 *    >> Redirect ESP_LOGX UART-Serial Messages to WebServer = 'Webserial' Logging   
 *    >> Read the electrical measurements via MODBUS from SMD device
 *    >> Publish the measured values to MQTT
 *    >> Provide OTA updates via mDNS (based URL) 
 * 
 * Where are the aync Websites updated with Powermeter values? 
 *    >> 'Interface_ModbusValues_to_WebServer_SDMValues'-Function
========================================================================================================*/
/*--------------------------------------------------------- 
  STEERING of Project capabilities
*---------------------------------------------------------*/
//#define CONFIG_PRM_MAIN_WEBSERIAL_USE // menuconfig: Use Webserial to forward logging messages
/*--------------------------------------------------------- 
   INCLUDES
*---------------------------------------------------------*/
#include "freertos/FreeRTOS.h"  // For FreeRTOS functions
#include "freertos/queue.h"     // For FreeRTOS queue functions
#include <time.h>               // For time, ctime, localtime, strftime
#include "esp_timer.h"          // Include for time measurement in microseconds
#include <math.h>               // For math functions like pow() and round()
#include "esp_littlefs.h"       // Use LittleFS to store the HTML page
#include "driver/gpio.h"        // For GPIO functions to set valid stage as early as possible
#include "nvs_flash.h"          // For NVS Flash functions, use to store 'lastBootReason'
// (my)Components
#include "xlan_connection.h"    // For connect_to_xlan() utilize ETHERNET or WIFI connection
#include "NTPSync_and_localTZ.h"// For NTP-Sync and Set local TZ
#define  SHRORT_TS_LEN (22)     // Length needed for short time-stamp string
#include "Modbus_UART_RTU.h"    // for Start Modbus RTU Serial
#include "myMQTT.h"             // For Start MQTT functions
#include "async_httpd_helper.h" // For Async HTTPD helper functions
#include "OTA_mDNS.h"           // For OTA and mDNS (based URL) 
/*--------------------------------------------------------- 
  ESP Logging: TAG 
*---------------------------------------------------------*/
#include <esp_log.h>                 // For ESP-logging
//                       "12345678"
static const char *TAG = "__MAIN__"; // TAG for logging
#include "esp_app_desc.h"            // For ESP-APP description
/*--------------------------------------------------------- 
  FIRMWARE: Global variables about current unning firmware   
*---------------------------------------------------------*/
const char *project_name;          // Project-Name      filled by 'app_main'
const char *firmware_version;      // Firmware-Version  filled by 'app_main'
char       firmware_build_ts[40];  // Firmware-Build-TS filled by 'app_main'
/*--------------------------------------------------------- 
  GLOBAL VARIABLES   
*---------------------------------------------------------*/
char s_ts[SHRORT_TS_LEN]= "-no error-";  // Short Time-Stamp for commonn use
/*--------------------------------------------------------- 
   SETTINGS of LAN + NTP-Time
*---------------------------------------------------------*/
//bool isLanConnected = false;     // Flag to determine if the LAN is connected
bool ntpTimeSet = false;         // Flag to check if NTP-Time is set
/*--------------------------------------------------------- 
  WEBSERVER: variables & defines
----------------------------------------------------------*/
httpd_handle_t handle_to_WebServer = NULL;              // Handle of the WebServer
#define TAG_WS                              "_WEB_SVR"  // TAG for logging in context of WebServer
bool lossOfWebserverConnection = false;                 // This flag is set if the WebServer connection is lost
/*--------------------------------------------------------- 
  NVS: Non-Volatile Storage
----------------------------------------------------------*/
bool isNVSready = false;                                // Store the status of NVS Flash initialization
/*--------------------------------------------------------- 
  MODBUS: defines and variables  
*---------------------------------------------------------*/
#define MB_UNIT_ID                            (1)       // Modbus Slave ID under which the SDM630 is reachable = This needs a SETTING at SDM630
#define MB_READ_INTERVAL_MS                   (2000)    // Interval in ms to read the SDM630
#define TAG_MB_READ                         "MB_R_REG"  // TAG for logging wehn reading Values from Modbus
void *ptr_Handle_to_Modbus_MasterController = NULL;     // Define a Pointer to Mobus-Controler  Define a variable to hold the Modbus controller handle
unsigned long readDataSetTime =               350;      // Last duration of read all Registers from Modbus (INIT with dummy, in ms)
TaskHandle_t modbus_poll_task_handle = NULL;            // Handle for the Modbus poll task        
/*--------------------------------------------------------- 
  MQTT: defines and variables  
*---------------------------------------------------------*/
#define CONFIG_MQTT_PUBLISH_INTERVAL_PWR      (2000)    // Intervall to re-Publish PRIROZED Powermeter-Values to MQTT - Invervall time in ms
#define CONFIG_MQTT_PUBLISH_NORMAL_FCT        (15)      // This factor defines the intervall for NONE-Prio Values: x-times LESS PRIO frequency
#define CONFIG_MQTT_PUBLISH_INTERVAL_ESP      (130000)  // Intervall to publish ESP's free heap size to MQTT - 3min
#define MQTT_MEASUREMENT_SUB_TOPIC            "MEA"     // Sub-Topic for Measument values
#define MQTT_ESP_SUB_TOPIC                    "ESP"     // Sub-Topic for ESP-Informations
#define MQTT_PRM_SUB_TOPIC                    "PRM"     // Sub-Topic for PowerMeter-Common-Informations
esp_mqtt_client_handle_t handle_to_MQTT_client = NULL;  // Init: Handle to MQTT client
#define TAG_MB_PUBL                         "MQ_P_REG"  // TAG for logging when publishing Modbus-Values to MQTT
#define TAG_ESP_PUBL                        "MQ_P_ESP"  // TAG for logging when publishing ESP-Values to MQTT
#define TAG_COM_PUBL                        "MQ_P_COM"  // TAG for logging when publishing Common Infos to MQTT
unsigned long publishDataSetTime =            100;      // Last duration of Publish to MQTT (INIT with dummy) in ms
volatile uint32_t powermeter_published_success = 0;     // COUNTER of complete successful cycles
volatile uint32_t powermeter_published_error = 0;       // COUNTER of publish errors in a cycle 
char powermeter_PublishSuccess_TS[SHRORT_TS_LEN]="-no publish-";// Time-Stamp last successful publishing to MQTT
char powermeter_PublishError_TS[SHRORT_TS_LEN]= "-no error-";   // Time-Stamp lasst error publishing to MQTT
TaskHandle_t mqtt_publish_task_handle_PRM = NULL;        // Handle for the MQTT publish task
/*--------------------------------------------------------- 
  CONFIGURATION <SDM> electrical measurements (modbus)  
*---------------------------------------------------------*/
#define PRM_Name "Eastron SDM630-V2-MODBUS"              // Name of the PowerMeter-Device the is used
#include "SDM.h"                                         // List of PowerMeter-REGISTERS, here for serval SDM Devices
volatile esp_err_t 
              ErrorCode_RegisterRead=0;                  // Last Error-Code of reading the register >> 0 = NO Error
const char *str_Error_RegisterRead;                      // Define & Init readable Error-Message
volatile uint32_t powermeter_reads_success = 0;          // COUNTER of complete successful cycles
volatile uint32_t powermeter_reads_error = 0;            // COUNTER of read errors  in  a  cycle 
char powermeter_SuccessUpdateDS_TS[SHRORT_TS_LEN] = "-no read-";// Time-Stamp last successful reading of complete DS
char powermeter_ErrorRead_TS[SHRORT_TS_LEN] = "-no error-";     // Time-Stamp first 'this' err occours               

// Doxygen documentation for the function below
/** --------------------------------------------------------------------------------------------------
 * @brief  [Short description of what the function does in one sentence.]
 *
 * [Longer description if needed. Describe the overall purpose of the function, its context in the system,
 *  and any notable behavior.]
 *
 * @param[in]  param_name   [Description of input parameter. Explain what the input is and how it is used.]
 * @param[out] param_name   [Description of output parameter. Explain what data is returned through this parameter.]
 *                          [Repeat @param for each input/output parameter as needed.]
 *
 * @return [Type]           [Describe what the function returns, including all possible return codes and their meanings.]
 *                          e.g., `ESP_OK` on success, `ESP_FAIL` on failure, etc.
 *
 * @note
 *   - [Any important notes: assumptions, side effects, timing considerations, etc.]
 *   - [Example: Must be called after initialization of the network interface.]
 *
 * @see
 *   - [Related functions or macros, e.g., `esp_netif_dhcpc_stop()`, `esp_netif_set_ip_info()`]
 *   - [Any modules or external references that this function relates to.]
 *
 * @warning
 *   - [Any dangerous usage scenarios, edge cases, or potential pitfalls.]
 *   - [Example: This function disables DHCP. Ensure no active leases depend on dynamic IP.]
 *-------------------------------------------------------------------------------------------------*/

/*#################################################################################################################################
   HELPERS   HELPERS   HELPERS   HELPERS   HELPERS   HELPERS   HELPERS   HELPERS   HELPERS   HELPERS   HELPERS   HELPERS   HELPERS
##################################################################################################################################*/

/** ------------------------------------------------------------------------------------------------
 * @brief  HELPER function to append to a string
 * 
 * This function appends a formatted string to a dynamically allocated string.
 * It uses `vasprintf` to format the string and `realloc` to resize the destination string.
 * 
 * @param[out] dest   Pointer to the destination string where the formatted string will be appended.
 * @param[in]  format Format string for the new piece to append.
 * 
 * @note  
 *    used by `Interface_ModbusValues_to_WebServer_SDMValues`
 *  -----------------------------------------------------------------------------------------------*/
static void Helper_AppendTo_String(char **dest, const char *format, ...)
{   va_list args;             // Declare Variable of type va_list, used to hold the arguments passed to the function.  <stdarg.h>
    va_start(args, format);   // Initialize the va_list variable with the format string
    char *new_piece;          // Declares a pointer to a char that should be added newly 
    int len = vasprintf(&new_piece, format, args); // allocate new formatted string
    va_end(args);             // Clean up the va_list variable after it has been used.  Ensures resources released.
    if (len == -1) { ESP_LOGE(TAG, "vasprintf failed"); return; } // check for allocation error
    if (*dest == NULL) { *dest = new_piece;   // If NULL it is the fist piece
    } else { 
        size_t old_len = strlen(*dest);       // Get length of the existing string
        *dest = realloc(*dest, old_len + len + 1); // Reallocate memory for the new string (make bigger)
        strcat(*dest, new_piece);             // append the new piece to the existing string to *dest
        free(new_piece);                      // Free the memory allocated for new_piece
    }
}

/** ------------------------------------------------------------------------------------------------
 * @brief  HELPER to get runtime of the ESP32 as formatted string
 * 
 * This function retrieves the uptime of the ESP32 in days, hours, minutes, and seconds,
 * and formats it into a string.
 * 
 * @return  char*  Returns a pointer to a static string containing the formatted uptime.
 * 
 * @note  
 *    used by `Interface_ModbusValues_to_WebServer_SDMValues`
 *  -----------------------------------------------------------------------------------------------*/
char* get_ESP_Uptime()
{   uint8_t seconds, minutes, hours; uint16_t days;
    // Get the ESP32 uptime in seconds
    uint32_t uptime = esp_timer_get_time() / 1000000; // Convert microseconds to seconds
    // Split / Convert the uptime into: sec, min, hours, days
    seconds = uptime % 60;        // Calculate seconds
    minutes = (uptime / 60) % 60; // Calculate minutes
    hours = (uptime / 3600) % 24; // Calculate hours
    days = uptime / 86400;        // Calculate days
    // Convert to a formatted string
    static char buffer[50];       // Allocate enough space for the timestamp string
    sprintf(buffer, "%4ud:%02d:%02d:%02d", days, hours, minutes, seconds); // create the string to display 
    return buffer;                // Return the dynamically allocated string
};

/** ------------------------------------------------------------------------------------------------
 * @brief  HELPER function to reboot the ESP32 with 3sec a countdown
 * 
 * @param[in]  TAG  Pointer to a string containing the tag for logging.
 * 
 * @note  
 *    used by `Interface_ModbusValues_to_WebServer_SDMValues`
 *  -----------------------------------------------------------------------------------------------*/
void Helper_Reboot_with_countdown( const char *TAG)
{   ESP_LOGE(TAG, "⚠️ ⚠️ REBOOT of ESP is triggerer after 3sec ⚠️ ⚠️");
    for (int i = 3; i > 0; i--) { // Show a countdown
                    ESP_LOGE(TAG_WS,"🔔 in %d seconds... 🔔", i);
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 sec            
                }
    esp_restart();                   // Reboot the ESP32
}

/** ------------------------------------------------------------------------------------------------
 * @brief  HELPER function store a string to NVS Flash to a given key
 * 
 * @param[in]  key_str        Pointer to a string containing the key under which the string will be stored.
 * @param[in]  str_to_store   Pointer to a string containing the value to be stored.
 * 
 * @return  esp_err_t  Returns the status of the NVS Flash operation.
 *
 * @note  
 *    used by `...`
 *  -----------------------------------------------------------------------------------------------*/
esp_err_t Helper_store_str_to_nvs(const char* key_str, const char* str_to_store)
{   /*----------------------------------------------------------- 
      Define/Init variables 
    -----------------------------------------------------------*/
    nvs_handle_t nvs_handle; // Handle for NVS storage
    esp_err_t err;          // Error code for NVS operations
    /*----------------------------------------------------------- 
      Open the NVS storage with read/write access. 
    -----------------------------------------------------------*/ 
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Faild to open NVS for read/write: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;} // exit
    /*----------------------------------------------------------- 
      Save string to NVS storage under the key given by 'key_str' 
    -----------------------------------------------------------*/ 
    err = nvs_set_str(nvs_handle, key_str , str_to_store);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to set last boot reason: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;} // exit}
    /*----------------------------------------------------------- 
      Try to commit the changes to NVS storage.
    -----------------------------------------------------------*/ 
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to commit last boot reason: %s", esp_err_to_name(err));}
    /*----------------------------------------------------------- 
      Close the NVS handle to release resources.
    -----------------------------------------------------------*/ 
    nvs_close(nvs_handle);
    return err;
}

/** ------------------------------------------------------------------------------------------------
 * @brief  HELPER function to read a string from NVS Flash by key
 * 
 * @param[in]  key_str  Pointer to a string containing the key under which the string is stored.
 * 
 * @return  char*  Returns a pointer to the read string (caller must free), or NULL if not found/error
 * 
 * @note  
 *    - Caller is responsible for freeing the returned string with free()
 *    - Returns NULL if key not found or on error
 *  -----------------------------------------------------------------------------------------------*/ 
char* Helper_read_from_nvs_with_key(const char* key_str)
{   /*----------------------------------------------------------- 
      Define/Init variables 
    -----------------------------------------------------------*/
    nvs_handle_t nvs_handle;  // Handle for NVS storage
    esp_err_t err;            // Error code for NVS operations
    char* read_str = NULL;    // String to return
    /*----------------------------------------------------------- 
      Open the NVS storage with read access. 
    -----------------------------------------------------------*/ 
    err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "NVS: Namespace 'storage' not found, creating it now");
            err = Helper_store_str_to_nvs("storage", ""); // Create the namespace if it does not exist
        } else {
            ESP_LOGE(TAG, "NVS: Failed to open NVS for read: %s", esp_err_to_name(err));
            return NULL;
        }
    }
    /*----------------------------------------------------------- 
      READ value(string) at key-tring from NVS storage 
    -----------------------------------------------------------*/ 
    size_t required_size = 0; // Variable to hold the size of the string to be read
    err = nvs_get_str(nvs_handle, key_str , NULL, &required_size); // Get the size of the string  
    if (err == ESP_OK) {
        //-------------------------------------------------------  
        // NO ERROR = key exists
        //-------------------------------------------------------
        // Allocate memory for the string based on the required size
        read_str = malloc(required_size); //  allocate memory for the string
        if (read_str == NULL) {
            ESP_LOGE(TAG, "NVS: Failed to allocate memory for key '%s'", key_str);
            nvs_close(nvs_handle);
            return NULL;
        }
        // READ the string from NVS 
        err = nvs_get_str(nvs_handle, key_str, read_str, &required_size);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "NVS: Read key '%s': %s", key_str, read_str);
        } else {
            ESP_LOGE(TAG, "NVS: Failed to read string for key '%s': %s", key_str, esp_err_to_name(err));
            free(read_str);
            read_str = NULL;
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG, "NVS: Key '%s' not found in NVS - this is normal on first boot", key_str);
        // Write the string 'First boot after flashing' to read_str to be returned
        read_str = malloc(40); // Allocate memory for the string
        if (read_str != NULL) {
            snprintf(read_str, 40, "Normal boot, no Last-Boot-Message"); // Write as answer to read_str
        } else {
            ESP_LOGE(TAG, "Failed to allocate memory for default string");
            nvs_close(nvs_handle);
            return NULL;
        }
    } else {
        ESP_LOGE(TAG, "Error reading key '%s' from NVS: %s", key_str, esp_err_to_name(err));
        read_str = NULL;
    }
    nvs_close(nvs_handle);
    return read_str; // Return the string or NULL
}

/*#################################################################################################################################
  MODBUS   SDM   MODBUS   SDM   MODBUS   SDM   MODBUS   SDM   MODBUS   SDM   MODBUS   SDM   MODBUS   SDM   MODBUS   SDM   MODBUS  
##################################################################################################################################*/
/*--------------------------------
  Structure for each SDM Register 
  used by: powermeter_RegArray
----------------------------------*/ 
typedef struct {
  int            cid;           // CID for Modus-Controller Structure 
  const char     *topicName;    // Topic name used for MQTT publish & Modbus-Controller-Structure
  const char     *unitOfValue;  // Unit of the Value
  float          currVal;       // Start Value Value of Register
  int            minVal;        // Min Value of Register for Modus-Controller Structure 
  int            maxVal;        // Max Value of Register for Modus-Controller Structure 
  int            digits;        // Digits for conversion to char* used by WebServer to Display Data
                                // AND to determine if value have changes (significanlty enough) see updateMQTT
  bool           hasPrio;       // Prio flag is used to send this value more often
  const uint16_t registerHex;   // Register-No as HEX
  bool           updateMQTT;    // Flag indicates it value have changes (significanlty enough) to be re-published to MQTT     
} powermeter_struct;

volatile powermeter_struct powermeter_RegArray[] = {
/*------------------------------------------------------------------------------------------------------------------------
   ARRAY with selected SDM Registers that should be requested frequently
---------------------------------------------------------------------------------------------------------------------------
 !!  IMPORTANT  !!  IMPORTANT  !!  IMPORTANT  !!  IMPORTANT  !!  IMPORTANT  !!  IMPORTANT  !!  IMPORTANT  !!  IMPORTANT
 !!  cid-Numbers NEED to start with 0!!! and increased by one for every further entry!
 Why? 
   * Otherwise belows 'powermeter_param_descriptors' will NOT be accepted by 'mbc_master_set_descriptor'
                       >> Error: Message: "Invalid CID"
   * The cid-Numbers seem to be used as Array-index with while identify the register in the Modbus-Controller structure.
------------------------------------------------------------------------------------------------------------------------*/  
//cid, topicName,    Unit,  currVal,  min-,maxVal,digits, hasPrio, registerHex, updateMQTT 
  {0, "Power-Total",  "W",   999.0,   0,72000,    0,       true,   SDM_TOTAL_SYSTEM_POWER, false},          // 0
  {1, "Frequency",    "HZ",  99.99,   0,60,       2,       true,   SDM_FREQUENCY, false},                   // 1
  {2, "ReactiveP",    "W",   999.0,   0,72000,    0,       false,  SDM_TOTAL_SYSTEM_REACTIVE_POWER, false}, // 2
  {3, "ApparentP",    "W",   999.0,   0,72000,    0,       false,  SDM_TOTAL_SYSTEM_APPARENT_POWER, false}, // 3
  {4, "Neutral-Curr", "A",   99.99,   0,100,      2,       false,  SDM_NEUTRAL_CURRENT, false},             // 4
  {5, "L1-3-Curr",    "A",   99.99,   0,100,      2,       false,  SDM_SUM_LINE_CURRENT, false},            // 5
  {6, "PFactor",      "PF",  9.99,    0,10,       2,       false,  SDM_TOTAL_SYSTEM_POWER_FACTOR, false},   // 6
  {7, "Energy-Sum",   "kWh", 99999.0, 0,99999,    2,       false,  SDM_IMPORT_ACTIVE_ENERGY, false},        // 7

  {8, "Power-L1",     "W",   999.0,   0,72000,    0,       false,  SDM_PHASE_1_POWER, false},               // 8
  {9, "Power-L2",     "W",   999.0,   0,72000,    0,       false,  SDM_PHASE_2_POWER, false},               // 9
  {10,"Power-L3",     "W",   999.0,   0,72000,    0,       false,  SDM_PHASE_3_POWER, false},               // 10
  {11,"ReactiveP-L1", "W",   999.0,   0,72000,    0,       false,  SDM_PHASE_1_REACTIVE_POWER, false},      // 11
  {12,"ReactiveP-L2", "W",   999.0,   0,72000,    0,       false,  SDM_PHASE_2_REACTIVE_POWER, false},      // 12
  {13,"ReactiveP-L3", "W",   999.0,   0,72000,    0,       false,  SDM_PHASE_3_REACTIVE_POWER, false},      // 13

  {14,"Voltage-L1",   "V",   999.0,   0,300,      1,       true,   SDM_PHASE_1_VOLTAGE, false},             // 14
  {15,"Voltage-L2",   "V",   999.0,   0,300,      1,       false,  SDM_PHASE_2_VOLTAGE, false},             // 15
  {16,"Voltage-L3",   "V",   999.0,   0,300,      1,       false,  SDM_PHASE_3_VOLTAGE, false},             // 16
  {17,"Current-L1",   "A",   99.99,   0,100,      2,       false,  SDM_PHASE_1_CURRENT, false},             // 17
  {18,"Current-L2",   "A",   99.99,   0,100,      2,       false,  SDM_PHASE_2_CURRENT, false},             // 18
  {19,"Current-L3",   "A",   99.99,   0,100,      2,       false,  SDM_PHASE_3_CURRENT, false}              // 19
};
#define MBREG (sizeof(powermeter_RegArray)/sizeof(powermeter_RegArray[0])) // Number of Selected SDM registers to read +1!!. READ-TIME per Register= ~22ms

mb_parameter_descriptor_t powermeter_param_descriptors[MBREG]; // Create an empty array of parameter descriptors for all my SDM registers 
/*---------------------------------------------------------------------------------------------------------
  Modbus_Build_ParaDescriptors_PowerMeter: Build the list of selected SDM registers for ESP-IDF's Modbus-Controller
  ---------------------------------------------------------------------------------------------------------
  used by: app_main 
           as handover to initialize the Modbus-Controller 
----------------------------------------------------------------------------------------------------------*/
void Modbus_Build_ParaDescriptors_PowerMeter(void) {
  for (int i = 0; i < MBREG; i++) { // Fill the empty array with the values from powermeter_RegArray
      powermeter_param_descriptors[i].cid            = i;                                 // [uint16_t],unsig 16-b int    >> CID for Modbus-Controller Structure / 
      powermeter_param_descriptors[i].param_key      = powermeter_RegArray[i].topicName;  // ➡️[const char]:              >> Meaning of the Register same like MQTT topic-name
      powermeter_param_descriptors[i].param_units    = powermeter_RegArray[i].unitOfValue; // ➡️[const char]:             >> The physical unit
      powermeter_param_descriptors[i].mb_slave_addr  = MB_UNIT_ID;                    // [uint8t],unsign.8-bit int        >> Address of the Slave Device to be read from
      powermeter_param_descriptors[i].mb_param_type  = MB_PARAM_INPUT;                // [enum] Modbus Pare-Types         >> What kind of Modbus register you want to access and how?   
                                          // Possible Types={MB_PARAM_HOLDING = Holding Reg., MB_PARAM_INPUT= Input Reg., MB_PARAM_COIL=Coils/boolean, MB_PARAM_DISCRETE= Discrete bits}
      powermeter_param_descriptors[i].mb_reg_start   = powermeter_RegArray[i].registerHex; // [uint16_t],unsig 16-b int   >> Modbus register address, hex
      powermeter_param_descriptors[i].mb_size        = PARAM_SIZE_FLOAT;              // [uint16_t],unsig 16-b int        >> Size of MB Parameter in registers
      powermeter_param_descriptors[i].param_offset   = (i*2);                         // [uint32t],unsign.32-bit int      >> Parameter name (OFFSET in the parameter structure or address of instance) // offsetof(powermeter_struct, currVal)
      powermeter_param_descriptors[i].param_type     = PARAM_TYPE_FLOAT_CDAB;         // [enum] >> Includes the encoding! >> Float, U8, U16, U32, ASCII, and very specialed ones thie above like used  PARAM_TYPE_FLOAT_CDAB
                                                                               //           SDM630: 32-bit IEEE-754 floating point: Big-endian byte order, 2 Modbus registers
      powermeter_param_descriptors[i].param_size     = PARAM_SIZE_FLOAT;              // Number of bytes in the Register.
      /*         PARAM_TYPE_FLOAT_CDAB >> WORKS // Big-endian byte order, 2 Modbus registers, reversed register order
                 PARAM_TYPE_FLOAT_ABCD >> ERROR // Big-endian byte order, 2 Modbus registers
                 PARAM_TYPE_FLOAT_BADC >> ERROR // Little-endian byte order, 2 Modbus registers, reversed register order
                 PARAM_TYPE_FLOAT_DCBA >> ERROR // Little-endian byte order, 2 Modbus registers */     
      // Info about the Value-Range and precision of the value  
      powermeter_param_descriptors[i].param_opts.min  = powermeter_RegArray[i].minVal; // Minimal Value. !!! NOT USED  in my Prj.!!!
      powermeter_param_descriptors[i].param_opts.max  = powermeter_RegArray[i].maxVal; // Maximal Value. !!! NOT USED  in my Prj.!!!
      powermeter_param_descriptors[i].param_opts.step = powermeter_RegArray[i].digits; // Step of parameter change tracking.
      powermeter_param_descriptors[i].access          = PAR_PERMS_READ;                // Access permissions based on mode
  }
}

/*================================================================================
   Task_Modbus_SDM_Poll_RegisterValues():
   Poll the SDM registers and update the values in the powermeter_RegArray
   used by: app_main 
=================================================================================*/
void Task_Modbus_SDM_Poll_RegisterValues(void *arg) {
  float value = 0.0f;                           // Define & Init value to read the register
  uint8_t type = 0;                             // Define & Init type to read the register
  esp_err_t err= ESP_OK;                        // Define & Init error code
  int64_t start_time;                           // Define Start time for the task
  int64_t elapsed_time;                         // Define Time spend with reading the registers
  bool flag_Cycle_Read_Error;                   // Error-Flag, when at least one Register fails
  /* COUNTERS                    (never resets)
    - powermeter_reads_success  of Successful read cycles
    - powermeter_reads_error    of num of times reads with error 
  * FLAGS
    - flag_Cycle_Read_Error     When at least one Register read-fails in cycle
  * ERROR-Code
    - ErrorCode_RegisterRead     Last Error-Code of reading the register >> 0 = NO Error
  * TIME-STAMPS
    - powermeter_ErrorRead_TS        Time-Stamp first 'this' err occours
    - powermeter_SuccessUpdateDS_TS  Time-Stamp last successful reading of complete DS
*/
  // Infinite loop to poll the registers
  while (1) {
      //--------------------------------------------------
      // If OTA is in progress, just do nothing and wait
      //--------------------------------------------------
      if (is_ota_update_in_progress()) { // Check if an OTA update is in progress
          vTaskDelay(pdMS_TO_TICKS(MB_READ_INTERVAL_MS)); // Just Wait
          continue; }                   // Skip the rest of the loop and check again
      //--------------------------------------------------
      // START of the cycle
      //--------------------------------------------------
      flag_Cycle_Read_Error = false;        // Reset the error flag 
      start_time = esp_timer_get_time();    // Get the Start-Time of reading in microseconds
      //========================================== 
      // START CYCLE (loop) through all registers
      //==========================================
      for (int i = 0; i < MBREG; i++) { 
          err = mbc_master_get_parameter(ptr_Handle_to_Modbus_MasterController, powermeter_param_descriptors[i].cid, (uint8_t*)&value, &type); // Read NEXT register   
          // Check if the current read was successful
          if (err == ESP_OK ) { 
              // SUCCESSFUL ✅
              ESP_LOGD(TAG_MB_READ, "--  ✅ Updated %s = %.2f [%s]", powermeter_RegArray[i].topicName,  value,  powermeter_RegArray[i].unitOfValue);
              //.......................................................................
              // CHECK if value has changed significantly and needs re-publish to MQTT
              //.......................................................................
              // Is there an not conducted MQTT-Publish alraday? >> Then not check if the value has changed
              if (!powermeter_RegArray[i].updateMQTT ) // NO former UPDATE PENDING then do it
              { // Check if the value has changed significantly for MQTT re-publish
                powermeter_RegArray[i].updateMQTT= (                                             // Set the flag according to the following condition
                    roundf(powermeter_RegArray[i].currVal * powf(10, powermeter_RegArray[i].digits))  // Round the current value
                    !=                                                                           // NOT SAME?
                    roundf(value *  powf(10, powermeter_RegArray[i].digits)));                   // Round the new value
                powermeter_RegArray[i].currVal = value;   // Update the current value in the powermeter_RegArray
              }
          } else {
              // ERROR ❌ 
              flag_Cycle_Read_Error = true;                         // Set the error flag
              powermeter_reads_error++;                             // Increment the error counter
              ESP_LOGE(TAG_MB_READ, "--  ⚠️ Failed reading CID %d (%s): %s", powermeter_param_descriptors[i].cid, powermeter_RegArray[i].topicName, esp_err_to_name(err) );
              break;}; // STOP the Cycle, no further register will be read        
      }; 
      //==========================================
      // CYCLE END
      //==========================================
      // PROCESS the result of the cycle (=LOGIC)
      //------------------------------------------
      if (flag_Cycle_Read_Error) {
          // ERROR ❌ 
          // Check if exactly 'this'-error already occures before?
          if (err!=ErrorCode_RegisterRead) {
              // * NO *  : First time 'this' error occurs
              ErrorCode_RegisterRead = err;                           // Save the new error code
              getShortTimesStamp(powermeter_ErrorRead_TS, sizeof(powermeter_ErrorRead_TS));// Save the time-stamp of 'this' error
              str_Error_RegisterRead = esp_err_to_name(err);          // Save the error message
              ESP_LOGE(TAG_MB_READ, "--  ❌ NEW Error occoured: %s", str_Error_RegisterRead);
              if (err==ESP_ERR_TIMEOUT) { // Check if the last read was a timeout
              ESP_LOGE(TAG_MB_READ, "--     >> Consider to increase time-out by using menconfig. Currently is %d ms.", CONFIG_MY_MB_REGISTER_REPONSE_TIMEOUT);
              };

          } else {
              // * YES * : Same error occurs again
              ESP_LOGD(TAG_MB_READ, "--  ⚠️ Cycle reads with errors: %"PRIu32, powermeter_reads_error);
          };
      } else {
          // SUCCESS ✅ = Complete cycle-read was successful
          powermeter_reads_success++;                                 // Update the number of successful reads
          ESP_LOGI(TAG_MB_READ, "--  ✅ Cycle read successful: %"PRIu32, powermeter_reads_success);
          ErrorCode_RegisterRead = ESP_OK;                            // Reset the error code
          getShortTimesStamp(powermeter_SuccessUpdateDS_TS, sizeof(powermeter_SuccessUpdateDS_TS));// Save the time-stamp of the last successful read
      };
      //------------------------------------------
      // Get Cycle time needed to read Registers
      //------------------------------------------
      elapsed_time = (esp_timer_get_time()-start_time)/1000;          // Time spend with reading the registers
      readDataSetTime = elapsed_time;                                 // Save the time to showed by the WebServer
      ESP_LOGD(TAG_MB_READ, "--  Needed time to read all registers: %lld ms", elapsed_time);
      //------------------------------------------
      // Idle to the end of the cycle-time
      //------------------------------------------
      vTaskDelay(pdMS_TO_TICKS(MB_READ_INTERVAL_MS-elapsed_time));    // Wait until start next cycle
    }; // END of the infinite loop
}; // END of the Task-Function

/*#################################################################################################################################
   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT   MQTT 
##################################################################################################################################*/

/** ------------------------------------------------------------------------------------------------
 * @brief  Publish the PowerMeter values to MQTT.
 * 
 * Build the MQTT payload from the current values in `powermeter_RegArray` 
 * and publish it to the MQTT broker.
 * 
 * @param[in]  i           Index of the public `powermeter_RegArray` to define value to publish.
 * @param[in]  publish_TS  Pointer to a string containing the timestamp of the last update.
 * @return     esp_err_t   Returns the status of the publish operation.
 *                         e.g., `ESP_OK` on success, `ESP_FAIL` on failure, etc.
 * @note
 *   used by `Task_MQTT_PowerMeter_Publish()`.
 *  -----------------------------------------------------------------------------------------------*/
esp_err_t MQTT_Publish_PWR_Values(int i /* index of arrray */, const char *publish_TS)
{ int msg_id;                                         // Define & Init message ID
  esp_err_t err= ESP_OK;                              // Define & Init error code
  char msg_payload[256];                              // Define & Init the message to be sent
  /*........................................................................................
     Build the Payload to be send
     {"value":"0.77","unit":"A","comment":"Current-L3","lastUpdate":"2025-05-14@19:31:24"}
  ..........................................................................................*/
  strcpy(msg_payload, "{\"value\":\"");               // JSON-Value 
  sprintf(msg_payload+strlen(msg_payload), "%.*f",    // Add Value
      powermeter_RegArray[i].digits,
      powermeter_RegArray[i].currVal); 
  strcat(msg_payload, "\",\"unit\":\"");              // JSON-Unit
  strcat(msg_payload, powermeter_RegArray[i].unitOfValue); // Add Unit
  strcat(msg_payload, "\",\"comment\":\"");           // JSON-comment
  strcat(msg_payload, powermeter_RegArray[i].topicName);   // Add comment
  strcat(msg_payload, "\",\"lastUpdate\":\"");        // JSON-Last update
  strcat(msg_payload, publish_TS);                    // Add Last update
  strcat(msg_payload, "\"}");                         // JSON- closing bracket
  /*........................................................................................
    Build the Topic
    'Power-Meter/MEA/Current-L3'
  ..........................................................................................*/
  char topic[128];                                    // Define & Init the topic to be sent
  snprintf(topic, sizeof(topic), "%s/%s/%s",     
         CONFIG_MY_MQTT_ROOT_TOPIC,                   // Root-Topic from MQTT menuconfig
         MQTT_MEASUREMENT_SUB_TOPIC,                  // Sub-Topic for Measurement definend here
         powermeter_RegArray[i].topicName);                // Topic name from the Measurement
  /*........................................................................................
    Publish the message to MQTT
  ..........................................................................................*/
  msg_id = esp_mqtt_client_publish(handle_to_MQTT_client,// MQTT client handle
         topic,                                       // Topic to publish
         msg_payload,                                 // Payload to send
         0,                                           // Message ID (0 for no response)
         CONFIG_MY_MQTT_QOS_DEFAULT,                  // QoS level
         CONFIG_MY_MQTT_RETAIN_DEFAULT);              // Retain flag
  if (msg_id >= 0) { // Check if the publish was successful
     err = ESP_OK;
      ESP_LOGD(TAG_MB_PUBL, "--  ✅ Published '%s' = %.*f[%s] - %s", 
         powermeter_RegArray[i].topicName, 
         powermeter_RegArray[i].digits, 
         powermeter_RegArray[i].currVal,
         powermeter_RegArray[i].unitOfValue,
         publish_TS); // Publish the value to MQTT
  }  else { 
      err = ESP_FAIL; 
  } 
  return err;
}  // END of the MQTT_Publish_PWR_Values

/** ------------------------------------------------------------------------------------------------
 * @brief  Publish One-Time PowerMeter measure to MQTT.
 * 
 * Build a JSON Payload with a received string and publish it so the target 'sub-topic' 
 * given with paramter.
 * 
 * @param[in]  sub_topic            Pointer to char: Subtopic-name for the element to publish.
 * @param[in]  element_topic        Pointer to char: Element-Topic to publish.
 * @param[in]  element_payload_str  Pointer to Payload-String.
 * 
 * @return     esp_err_t   Returns the status of the publish operation.
 *                         e.g., `ESP_OK` on success, `ESP_FAIL` on failure, etc.
 * @note
 *   used by `main`.
 *  -----------------------------------------------------------------------------------------------*/
esp_err_t MQTT_Publish_OneTime_Measure(const char *sub_topic, const char *element_topic, const char *element_payload_str)
{ int msg_id;                                         // Define & Init message ID
  esp_err_t err= ESP_OK;                              // Define & Init error code
  char msg_payload[256];                              // Define & Init the message to be sent
  /*........................................................................................
     Build the Payload to be send
     {"value":"will be the payload string", "comment":"Last-Boot-Time"}
  ..........................................................................................*/
  strcpy(msg_payload, "{\"value\":\"");       // JSON-Value
  strcat(msg_payload, element_payload_str);   // Add the payload string 
  strcat(msg_payload, "\",\"comment\":\"");   // JSON-comment
  strcat(msg_payload, element_topic);         // Add comment
  strcat(msg_payload, "\"}");                 // JSON- closing bracket
  /*........................................................................................
    Build the Topic
    'Power-Meter/<sub_topic>/<element_topic>'
  ..........................................................................................*/
  char topic[128];                            // Define & Init the topic to be sent
  snprintf(topic, sizeof(topic), "%s/%s/%s",     
         CONFIG_MY_MQTT_ROOT_TOPIC,           // Root-Topic from MQTT menuconfig
         sub_topic,                           // Sub-Topic for Measurement definend here
         element_topic);                      // Topic name from the Measurement
  /*........................................................................................
    Publish the message to MQTT
  ..........................................................................................*/
  msg_id = esp_mqtt_client_publish(handle_to_MQTT_client,// MQTT client handle
         topic,                                       // Topic to publish
         msg_payload,                                 // Payload to send
         0,                                           // Message ID (0 for no response)
         CONFIG_MY_MQTT_QOS_DEFAULT,                  // QoS level
         CONFIG_MY_MQTT_RETAIN_DEFAULT);              // Retain flag
  if (msg_id >= 0) { // Check if the publish was successful
      err = ESP_OK;
      ESP_LOGD(TAG_MB_PUBL, "--  ✅ Published '%s' = %s", 
         topic,
         element_payload_str);              
  }  else { 
      err = ESP_FAIL; 
  } 
  return err;
}  // END of the MQTT_Publish_OneTime_Measure

/** ------------------------------------------------------------------------------------------------
 * @brief  Publish the ESP's free heap size to MQTT.
 * 
 * @return     esp_err_t   Returns the status of the publish operation.
 * 
 * @note
 *    used by `Task_MQTT_publish_ESP_freeHeap`
 *  -----------------------------------------------------------------------------------------------*/
esp_err_t MQTT_publish_ESP_freeHeap()
{ esp_err_t err= ESP_OK;                              // Define & Init error code
  char msg_payload[256];                              // Define & Init the message to be sent
  /*........................................................................................
     Build the Payload to be send
     {"value":"114488","unit":"byte","comment":"ESP-freeHeap","lastUpdate":"2025-05-14@19:31:24"}
  ..........................................................................................*/
  strcpy(msg_payload, "{\"value\":\"");               // JSON-Value 
  sprintf(msg_payload+strlen(msg_payload), "%ld",      // Add Value: ESP's free heap size
         esp_get_free_heap_size()); 
  strcat(msg_payload, "\",\"unit\":\"");              // JSON-Unit
  strcat(msg_payload, "byte");                        // Add Unit
  strcat(msg_payload, "\",\"comment\":\"");           // JSON-comment
  strcat(msg_payload, "ESP-freeHeap");                // Add comment
  strcat(msg_payload, "\",\"ESP-uptime\":\"");        // JSON-Last update
  strcat(msg_payload, get_ESP_Uptime());              // Get the ESP uptime as string
  strcat(msg_payload, "\"}");                         // JSON- closing bracket  
   /*........................................................................................
    Build the Topic
    'Power-Meter/MEA/ESP_freeHeap'
  ..........................................................................................*/
  char topic[128];                                    // Define & Init the topic to be sent
  snprintf(topic, sizeof(topic), "%s/%s/%s",     
         CONFIG_MY_MQTT_ROOT_TOPIC,                   // Root-Topic from MQTT menuconfig
         MQTT_MEASUREMENT_SUB_TOPIC,                  // Sub-Topic for Measurement definend here
         "ESP-freeHeap");                             // Fixed Topic name for ESP free heap
  /*........................................................................................
    Publish the message to MQTT
  ..........................................................................................*/
  int msg_id = esp_mqtt_client_publish(handle_to_MQTT_client,// MQTT client handle
         topic,                                       // Topic to publish
         msg_payload,                                 // Payload to send
         0,                                           // Message ID (0 for no response)
         CONFIG_MY_MQTT_QOS_DEFAULT,                  // QoS level
         CONFIG_MY_MQTT_RETAIN_DEFAULT);              // Retain flag
  if (msg_id == 0) { // Check if the publish was successful
      err = ESP_FAIL; 
  } 
  return err;
} // END of the ESP_publish function

/** ------------------------------------------------------------------------------------------------
 * @brief  Publish Common Infos to MQTT with a Subtopic/Elment-Topic structure.
 * 
 * It sends looging messages to the console with ESP_LOGx.
 * 
 * @param[in]  sub_topic      Pointer to char: Subtopic-name for the element to publish.
 * @param[in]  element_topic  Pointer to char: Element-Topic to publish.
 * @param[in]  element_value  Pointer to char: Char string with Info to publish.
 * 
 * @note
 *    used by `app_main`
 *  -----------------------------------------------------------------------------------------------*/
//void MQTT_publish_Common_infos(const char *sub_topic, const char *element_topic, const char *element_value)
void MQTT_publish_Common_infos(const char *sub_topic, const char *element_topic, const char *element_value)
{ char msg_payload[100];                              // Define & Init the message to be sent
  /*........................................................................................
     Build the Payload to be send
  ..........................................................................................*/
  snprintf(msg_payload, sizeof(msg_payload), "%s", element_value); // Plain text value to be sent
  /*........................................................................................
    Build the Topic, Exmples:
    'Power-Meter/ESP/Project-Name'
    'Power-Meter/ESP/Source-Path'
    'Power-Meter/ESP/Firmware-Version'
    'Power-Meter/ESP/Build-Time'
    'Power-Meter/ESP/Chip-Name'    
  ..........................................................................................*/
  char topic[128];                                    // Define & Init the topic to be sent
  snprintf(topic, sizeof(topic), "%s/%s/%s",     
         CONFIG_MY_MQTT_ROOT_TOPIC,                   // Root-Topic from MQTT menuconfig
         sub_topic,                                   // Sub-Topic for the Info
         element_topic);                              // Topic name for the given element
  /*........................................................................................
    Publish the message to MQTT
  ..........................................................................................*/
  int msg_id = esp_mqtt_client_publish(handle_to_MQTT_client,// MQTT client handle
         topic,                                       // Topic to publish
         msg_payload,                                 // Payload to send
         0,                                           // Message ID (0 for no response)
         CONFIG_MY_MQTT_QOS_DEFAULT,                  // QoS level
         CONFIG_MY_MQTT_RETAIN_DEFAULT);              // Retain flag
  if (msg_id == 0) { // Check if the publish was successful
        // ERROR ❌ 
        ESP_LOGE(TAG_COM_PUBL, "--  ❌ Failed to Publish Common Info '%s' to MQTT", topic);
  } else {
        // SUCCESSFUL ✅
        ESP_LOGD(TAG_COM_PUBL, "--  ✅ Published Common Info '%s' to MQTT", topic);
  }
} // END of the MQTT_publish_Common_infos function

/** ------------------------------------------------------------------------------------------------
 * @brief  TASK-Handler to frequently publish the free heap of the ESP to MQTT.
 * 
 * Logges is success or failure to the console with ESP_LOGx. 
 * 
 * @note
 *    used by `app_main`
 *  -----------------------------------------------------------------------------------------------*/
void Task_MQTT_publish_ESP_freeHeap(void *arg)
{ while (1) { // Infinite loop of this task
      //------------------------------------------
      // START of the cycle
      //------------------------------------------
      esp_err_t err = MQTT_publish_ESP_freeHeap(); // Publish the ESP's free heap to MQTT
      if (err == ESP_OK ) { 
        // SUCCESSFUL ✅
        ESP_LOGD(TAG_ESP_PUBL, "--  ✅ Published ESP's free heap: %ld bytes", esp_get_free_heap_size());
      } else {
        // ERROR ❌ 
        ESP_LOGE(TAG_ESP_PUBL, "--  ❌ Failed to Publish ESP's free heap");
      }; 
      //------------------------------------------
      // Idle to the end of the cycle-time
      //------------------------------------------
      vTaskDelay(pdMS_TO_TICKS(CONFIG_MQTT_PUBLISH_INTERVAL_ESP)); // Wait until start next cycle
  } // End of the infinite loop 
} // END of the Task-Function  

/** ------------------------------------------------------------------------------------------------
 * @brief  TASK-Handler to check received PowerMeter-values and publish to MQTT if needed.
 * 
 * This function is called periodically to check the values in `powermeter_RegArray`
 * and publish them to MQTT if they have changed significantly.
 * 
 * @note
 *    used by `app_main`
 *  -----------------------------------------------------------------------------------------------*/
void Task_MQTT_PowerMeter_Publish(void *arg)
{ esp_err_t err= ESP_OK;                        // Define & Init error code
  int64_t start_time;                           // Define Start time for the task
  int64_t elapsed_time;                         // Define Time spend with reading the registers
  bool isNonePrioCycle = false;                 // Flag to determine if this is a NONE!-PRIO-Cycle
  bool flag_Cycle_Publ_Error;                   // Error-Flag, when at least one Register fails
  u_int8_t counterForNonePrioCycle = CONFIG_MQTT_PUBLISH_NORMAL_FCT-1;  // Init: That means >> First cycle is a NONE-PRIO-Cycle
  char publish_TS[22];                          // Time-Stamp used to publish measurements to MQTT
  strcpy(publish_TS, "2020-01-01@00:00:00");    // Init the time-stamp
  // .....................................................................................
  // INITAL wait time until the first publish cycle
  // This is makes sure that there holpefuly PowerMeter-Values are ready to be published
  // .....................................................................................
  vTaskDelay(pdMS_TO_TICKS(CONFIG_MQTT_PUBLISH_INTERVAL_PWR)); // Delay start of endless loop for 1x the publish interval
  while (1) { // Infinite loop of this task
      //--------------------------------------------------
      // If OTA is in progress, just do nothing and wait
      //--------------------------------------------------
      if (is_ota_update_in_progress()) { // Check if an OTA update is in progress
          vTaskDelay(pdMS_TO_TICKS(CONFIG_MQTT_PUBLISH_INTERVAL_PWR)); // Just wait for reboot of failed OTA
          continue; }                    // Skip the rest of the loop and check again
      //--------------------------------------------------
      // START of the cycle
      //--------------------------------------------------
      start_time = esp_timer_get_time();        // Get the Start-Time of reading in microseconds
      flag_Cycle_Publ_Error = false;            // Reset the error flag 
      getShortTimesStamp(publish_TS, sizeof(publish_TS)); // Generate the time-stamp used when publishing measurements to MQTT in this cycle
      // Determine if next is as NORMAL-Cycle to publish ALL Registers including without PRIO
      counterForNonePrioCycle++;                // Increment Cycle-counter PRIO's
      isNonePrioCycle = (counterForNonePrioCycle >= CONFIG_MQTT_PUBLISH_NORMAL_FCT); // Check if this is a NONE-PRIO-Cycle
      if (isNonePrioCycle) { 
          // NONE-PRIO-Cycle
          ESP_LOGD(TAG_MB_PUBL, "⏰  !ALL Cycle %d: Publish ALL registers including NONE-PRIO (in case of siginficant change)", counterForNonePrioCycle);
          //                     🕑  PRIO
      } else {
          // PRIO-Cycle
          ESP_LOGD(TAG_MB_PUBL, "🕑  PRIO Cycle %d: Publish only PRIO registers (in case of siginficant change)", counterForNonePrioCycle);
      };
      //========================================== 
      // START CYCLE (loop) through ALL registers
      //==========================================
      for (int i = 0; i < MBREG; i++) { 
          // For EACH SDM Value
          //........................
          // Check SKIPP conditions
          //........................
          // NOT a NONE-PRIO-Cycle but Register not have set the .hasPrio-Flag?
          if (!isNonePrioCycle && !powermeter_RegArray[i].hasPrio) { continue; } // SKIP this register as it has no PRIO-Flag
          // Value not changed significantly changed from last publish?
          if (!powermeter_RegArray[i].updateMQTT) { continue; }                  // SKIP this register as it has no update-Flag
          // ........................................................
          // PUBLISH-Section 
          // ........................................................
          //    only reached if conctions above not lead to > 'SKIP'
          // ........................................................
          err = MQTT_Publish_PWR_Values(i, publish_TS); // Publish the value to MQTT
          // Check if the publish was successful
          if (err == ESP_OK ) { 
              // SUCCESSFUL ✅
              powermeter_RegArray[i].updateMQTT= false; // Reset the update-Flag
              // Log-Message on sucess is integrated in the Publish-SDM-Function
              powermeter_published_success++;                              // Increment counter
          } else {
              // ERROR ❌ 
              powermeter_published_error++;                                // Increment counter
              ESP_LOGE(TAG_MB_PUBL, "--  ❌ Failed to Publish Value of = '%s'", powermeter_RegArray[i].topicName);
              flag_Cycle_Publ_Error = true;                         // Set the error flag
          };      
      }; 
      //==========================================
      // CYCLE END
      //==========================================
      // Reset the cycle-counter for the next NONE-PRIO-Cycle
      if (isNonePrioCycle) { counterForNonePrioCycle = 0; } 
      //==========================================
      // PROCESS the result of the cycle (=LOGIC)
      //------------------------------------------
      if (flag_Cycle_Publ_Error) {
          // ERROR ❌ 
          strcpy(powermeter_PublishError_TS, publish_TS);     // Save the time-stamp error publishing to MQTT
      } else {
          // SUCCESS ✅ 
          strcpy(powermeter_PublishSuccess_TS, publish_TS);   // Save the time-stamp of last successful publish
      };
      //-----------------------------------------------
      // Get Cycle time needed to re-publish Registers
      //-----------------------------------------------       
      elapsed_time = (esp_timer_get_time()-start_time)/1000;        // Time spend in last cycle
      publishDataSetTime = elapsed_time;                            // Save the time to showed by the WebServer
      ESP_LOGD(TAG, "--  Needed time to re-Publish all registers: %lld ms", elapsed_time);   
      //------------------------------------------
      // Idle to the end of the cycle-time
      //------------------------------------------
      vTaskDelay(pdMS_TO_TICKS(CONFIG_MQTT_PUBLISH_INTERVAL_PWR-elapsed_time)); // Wait until start next cycle
  } // END of the infinite loop
} // END of the Task-Function


/*#################################################################################################################################
  ESPLOGx to WebServer      ESPLOGx to WebServer      ESPLOGx to WebServer      ESPLOGx to WebServer         ESPLOGx to WebServer
##################################################################################################################################*/

/*--------------------------------------------------------- 
   VARIABLES & CONSTANTS for ESPLOGx to WebServer 
----------------------------------------------------------*/
#define MAX_MSG_SIZE  (250)  // Maximum size of a log message if bigger it will be truncated
#define QUEUE_LENGTH  CONFIG_PRM_MAIN_WEBSERIAL_QUEUE_SIZE  // Size of the log queue (menuconfig)
// Structure of each Queue Elemet
typedef char a_fmted_ESPLOGx_Message[MAX_MSG_SIZE];            // This is the type for my message to/from quue 
static QueueHandle_t  log_xQueue = NULL;                       // Handle to the queue, is ceated in app_main
#define QUEUE_WAIT_MS                    pdMS_TO_TICKS(100)    // 100ms Watinng time to grab a message from the queue
uint8_t openSocketCounter = 0;                                 // Counter for open sockets
vprintf_like_t orig_log_handler = NULL;                        // Pointer to the original ESP_LOGx handler

/*================================================================================
  Handle_ESPLOGx_custom(): Handle to forward ESPLOGx to queue for WebServer
       Logs are forwarded to the WebServer and to the original log handler
  used by: ??
=================================================================================*/
int Handle_ESPLOGx_custom(const char *fmt, va_list args) {
    //----------------------------------------------------
    // Write the log message to the original log handler
    //---------------------------------------------------
    if (orig_log_handler) {  // Check if the original log handler is set >> REPEAT it there
        va_list args_copy; va_copy(args_copy, args); // Make a copy va_list for reuse
        orig_log_handler(fmt, args_copy); }          // Send message to original (UART) log output 
    //------------------------------------
    // Forward to the Webserial log queue
    //------------------------------------
    // Check if the queue is full
    if (uxQueueSpacesAvailable(log_xQueue) == 0) { // Check if the queue is full
        // ESP_LOGW(TAG, "!!  ⚠️ Queue is full!");     // Log a warning message
        // Queue ist full remove oldest element to have space for a new one
        a_fmted_ESPLOGx_Message dummy; xQueueReceive(log_xQueue, &dummy, 0);  // Receive a element to get space for a new
    }
    a_fmted_ESPLOGx_Message fmted_msg;  // Will be filled formated message                           
    int lenAfterFmt = vsnprintf(fmted_msg, sizeof(fmted_msg), fmt, args); // Format the Message (make ready for the queue)
    if (lenAfterFmt > 0) {                   // Check if the message was formatted successfully
        if (! (xQueueSend(log_xQueue, &fmted_msg, 0) == pdPASS))   // Puts the message to queue to display with WebServer
        { ESP_LOGW(TAG, "!!  ⚠️ Failed place message to 'log_xQueue'!"); }
    }
    // HOOK on the ESP_LOGx-Stream to grab not direct accessible messages. 
    /*--------------------------------------------------------------------------------------------
      Search a messaages from http-daemnon of WebServer (Webserial) the states a connection loss
    ----------------------------------------------------------------------------------------------*/
    if ( strstr(fmted_msg, "httpd:")) {       // Only hook on http-daemon messages
        if (strstr(fmted_msg, "error")) {     // Only search for error messages
            if (strstr(fmted_msg, "113")) {   // Only error 113 = connection lost
               //E (12:19:55.094) httpd: httpd_accept_conn: error in accept (113)
               // This will lead to a REBOOT of the ESP32
               lossOfWebserverConnection = true; // Set the flag to indicate a connection loss
            }    
        } 
    }
    return lenAfterFmt;
}

/*#################################################################################################################################
  WEBSERVER    WEBSERVER    WEBSERVER    WEBSERVER    WEBSERVER    WEBSERVER    WEBSERVER    WEBSERVER    WEBSERVER    WEBSERVER
##################################################################################################################################*/

/** ------------------------------------------------------------------------------------------------
 * @brief  TASK-Handler to frequently if Webserver is brocken >> Then reboot the ESP32.
 * 
 * @note
 *    used by `app_main`
 *  -----------------------------------------------------------------------------------------------*/
void Task_is_webserver_connection_loss_then_reboot(void *arg)
{ while (1) { // Infinite loop of this task
      //------------------------------------------
      // ENDLESS LOOP
      //------------------------------------------
      // Start with waiting for the next cycle
      //------------------------------------------
      vTaskDelay(pdMS_TO_TICKS( 60 * 1000 )); // Check once per minute if the WebServer brocken down (flag is set)
      //------------------------------------------
      // Check if the Gateway was NOT reachable
      //------------------------------------------     
      if (lossOfWebserverConnection)
      { // It flag is set, that the WebServer was not reachable
          ESP_LOGE(TAG, "--  ❌ Webserver connection BROCKEN, rebooting ESP32"); // Log the error
          //--------------------------------------
          // Store the lastBootReason to NVS
          //--------------------------------------
          if (isNVSready) {
              esp_err_t err = Helper_store_str_to_nvs("lastBootReason", "HTTP-Daemon connection loss"); // Store the lastBootReason to NVS
              if (err != ESP_OK) { // Check if the store was successful
                  ESP_LOGE(TAG, "--     ❌ Failed store 'lastBootReason' in NVS: %s", esp_err_to_name(err)); // Log the error
              } else {  
                  ESP_LOGD(TAG, "--     ✅ Stored 'lastBootReason' in NVS: HTTP-Daemon connection loss"); // Log the success
              }
          }
          //--------------------------------------
          // Do reboot with countdown
          //--------------------------------------
          Helper_Reboot_with_countdown(TAG); // 3s Countdown, then rebooting
      }
  } // End of the infinite loop 
} // END of the Task-Function  

/*================================================================================
  Interface_ModbusValues_to_WebServer_SDMValues: BUILD proccess > creates a dynamic XML response
  used by: Handle_WebServer_SDM_Values_PUT
* Updates all values of the selected SDM registers and others @Website of WebServer
=================================================================================*/
static char* Interface_ModbusValues_to_WebServer_SDMValues() 
{   ESP_LOGD(TAG, "-- BUILD answer:");
    char *xml = NULL; // Declares a empty Pointer for the XML string     
    // Open XML-Tag 
    ESP_LOGD(TAG, "--   (1) Start: With openig TAG <xml>"); 
    Helper_AppendTo_String(&xml, "<xml>"); // Start with opening tag
    // Add measured electrical values to response
    ESP_LOGD(TAG, "--   (2) Add: Frequent measured electrical values");
    for (int i = 0; i < MBREG; i++) {
        Helper_AppendTo_String(&xml, "<response%d>" , i);                             // TAG opening <response%d> 
        Helper_AppendTo_String(&xml, "%.*f", powermeter_RegArray[i].digits, powermeter_RegArray[i].currVal ); // SMD Resigter Value WITH right Digits
        Helper_AppendTo_String(&xml, "</response%d>", i);                             // TAG closing <response%d> 
    }
    // Add Meta-data & others to response
    ESP_LOGD(TAG, "--   (3) Add: Meta Data of measuments & others");
    // TITLE with PowerMeter-Name
    Helper_AppendTo_String(&xml, "<prmname>%s</prmname>", PRM_Name);                       // Write PowerMeter- Name      </prmname>"
    // MODBUS
    Helper_AppendTo_String(&xml, "<sdmcnt>%d</sdmcnt>",    powermeter_reads_success);     // Counts sucess                </sdmcnt>" 
    Helper_AppendTo_String(&xml, "<errtotal>%d</errtotal>",powermeter_reads_error);       // Counts error                 </errtotal>"
    Helper_AppendTo_String(&xml, "<timest>%s</timest>",    powermeter_SuccessUpdateDS_TS);// Last successful time-stamp  </timest>"
    Helper_AppendTo_String(&xml, "<errorts>%s</errorts>",  powermeter_ErrorRead_TS);      // Last error time-stamp        </errorts>"
    Helper_AppendTo_String(&xml, "<lasterrtxt>%s</lasterrtxt>", str_Error_RegisterRead);  // Last 'this' error time-st.   </lasterrtxt>"
    // MQTT
    Helper_AppendTo_String(&xml, "<mqttcnts>%d</mqttcnts>",powermeter_published_success); // Counts sucess                </mqttcnts>"
    Helper_AppendTo_String(&xml, "<mqttcnte>%d</mqttcnte>",powermeter_published_error);   // Counts error                 </mqttcnte>
    Helper_AppendTo_String(&xml, "<mqtttss>%s</mqtttss>",  powermeter_SuccessUpdateDS_TS);// Last successful time-stamp   </mqtttss>"
    Helper_AppendTo_String(&xml, "<mqtttse>%s</mqtttse>",  powermeter_PublishError_TS);   // Last successful time-stamp   </mqtttse>"
    // ESP
    Helper_AppendTo_String(&xml, "<upt>%s</upt>", get_ESP_Uptime());                      // Uptime of this               </upt>"    
    u_int32_t hSize = esp_get_free_heap_size(); 
    Helper_AppendTo_String(&xml, "<freeh>%d.%d</freeh>",    hSize/1000,hSize%1000);       // Check the left HEAP memory   </freeh>"
    Helper_AppendTo_String(&xml, "<rganswtm>%d</rganswtm>", readDataSetTime/MBREG);       // Average Reg.-Read-Time       </rganswtm>"
    Helper_AppendTo_String(&xml, "<dsreadtm>%d</dsreadtm>", readDataSetTime);             // Cycle time over Regs         </dsreadtm>"
    // Running FIRMWARE
    Helper_AppendTo_String(&xml, "<fwname>%s</fwname>",      project_name);               // Firmware-Name               </fwname>"
    Helper_AppendTo_String(&xml, "<fwver>%s</fwver>",        firmware_version);           // Firmware-Version            </fwver>"
    Helper_AppendTo_String(&xml, "<fwbuildts>%s</fwbuildts>",firmware_build_ts);          // Firmware-Build              </fwbuildts>"
    Helper_AppendTo_String(&xml, "<chipname>%s</chipname>",  CONFIG_IDF_TARGET);          // Chip-Name                   </chipname>"   
    // Closing of XML-tag
    ESP_LOGD(TAG, "--   (4) End: With closing TAG </xml>"); 
    Helper_AppendTo_String(&xml, "</xml>");
    return xml; // remember: caller must free(xml)
}

/*================================================================================
  Handle_WebServer_SDM_Index_GET: Provide the inital Index-Page
  used by: start_PowerMeter_WebServer
=================================================================================*/
static esp_err_t Handle_WebServer_SDM_Index_GET(httpd_req_t *req)
{   ESP_LOGD(TAG_WS, "--  indexPage-Request -> Deliver...");
    //-----------------------------------------------
    // Open File from SPIFFS
    //-----------------------------------------------
    FILE *f = fopen("/rt_files/prm_webserver.html", "r"); // Read the WebPage from SPIFFS-Drive
    if (!f) {  // Check if the file was opened successfully
      ESP_LOGE(TAG_WS, "--  ❌ Failed to read 'prm_webserver.html' from SPIFFS.bin."); 
      httpd_resp_send_404(req); return ESP_FAIL; }
    //-----------------------------------------------
    // Delver WebPage
    //-----------------------------------------------
    char buf[512]; size_t read_bytes;                          
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) { // Read file in chunks
       httpd_resp_send_chunk(req, buf, read_bytes);            // Send chunk to client
    }
    fclose(f);                                                 // Close the file
    httpd_resp_send_chunk(req, NULL, 0);                       // End response
    return ESP_OK;    
}

/*================================================================================
  Handle_WebServer_SDM_Values_PUT: This function handles does the XML- PUT request
  used by: start_PowerMeter_WebServer
=================================================================================*/
static esp_err_t Handle_WebServer_SDM_Values_PUT(httpd_req_t *req)
{   ESP_LOGV(TAG_WS, "-- Received: XML PUT-Request");
    char *xml_str = Interface_ModbusValues_to_WebServer_SDMValues();
    if (xml_str == NULL) {httpd_resp_send_500(req);  } // Internal Server Errorreturn ESP_FAIL;
    // SEND the XML response
    size_t xml_len = strlen(xml_str); // Get the byte-length of the XML string
    httpd_resp_set_type(req, "text/xml");
    httpd_resp_send(req, xml_str, strlen(xml_str));    
    ESP_LOGD(TAG, "-- FIRED(done): Send update (%d bytes)",xml_len);
    // Clean & Go    
    free(xml_str); // important to free the generated string
    return ESP_OK;
}

/*================================================================================
  Handle_WebServer_Logging_Index_GET(): Serve the Starting HTML page
      * Handles the GET request for the root URL ("/webserial").
      * HTML page includes a JavaScript EventSource to receive log messages.
  used by: start_logwebserver()
=================================================================================*/
esp_err_t Handle_WebServer_Logging_Index_GET(httpd_req_t *req)
{   ESP_LOGD(TAG_WS, "--  log_webrsever indexPage-Request -> Deliver...");
    //-----------------------------------------------
    // Open File from SPIFFS
    //-----------------------------------------------
    FILE *f = fopen("/rt_files/webserial.html", "r"); // Read the WebPage from SPIFFS-Drive
    if (!f) {  // Check if the file was opened successfully
      ESP_LOGE(TAG_WS, "--  ❌ Failed to read 'webserial.html' from SPIFFS.bin."); 
      httpd_resp_send_404(req); return ESP_FAIL; }
    //-----------------------------------------------
    // Delver WebPage
    //-----------------------------------------------
    char buf[512]; size_t read_bytes;                          
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) { // Read file in chunks
       httpd_resp_send_chunk(req, buf, read_bytes);            // Send chunk to client
    }
    fclose(f);                                                 // Close the file
    httpd_resp_send_chunk(req, NULL, 0);                       // End response
    return ESP_OK;
}

/*================================================================================
  Handle_WebServer_Logging_ServerSentEvents_GET(): 
    * This function handles the GET request for the SSE stream.
      It works asyn-way be starts the async worker task to handle the request.
    * The handler will run on a separate thread to avoid blocking the main thread.
      means avoids blocking of the webserver. 
  used by: start_logwebserver()
=================================================================================*/
static esp_err_t Handle_WebServer_Logging_ServerSentEvents_GET(httpd_req_t *req)
{   /*  Sets HTTP Response Headers:
      - text/event-stream: Tells the browser this is an SSE stream.  
      - Cache-Control:     no-cache   >> Prevents caching.
      - Connection:        keep-alive >> Keeps the connection open. */    
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr( req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr( req, "Connection", "keep-alive");
    a_fmted_ESPLOGx_Message receivedMsg; // NO LEACKAGE:  Variable to store the received message for processing

    // This handler is first invoked on the httpd thread.
    // In order to free the httpd thread to handle other requests,
    // we must resubmit our request to be handled on an async worker thread.
    if (is_on_async_worker_thread() == false) {
        if (sumit_req_to_async_workers_queue(req, &Handle_WebServer_Logging_ServerSentEvents_GET) == ESP_OK) {
            ESP_LOGD(TAG_WS, "Submited request to NEW Async-Worker-Task");
            return ESP_OK;
        } else {
            ESP_LOGD(TAG_WS, "All async-worker-threads a busy. Wait for free worker.");
            httpd_resp_set_status(req, "503 Busy");
            return ESP_OK;
        }
    } else {
            ESP_LOGD(TAG_WS, "Request is already on an Async-Worker-Task");  
    }
    // +++++++++++++++++++++++++++++++++++++  ENDLESS LOOP  +++++++++++++++++++++++++++++++++++++
    while (true) 
    {   // Check if there are messages in queue
        bool areMsgsInQueue = uxQueueMessagesWaiting(log_xQueue); // Get the number of messages in the queue 
        if (areMsgsInQueue >=1 ) 
        {   // PROCESSING of Log-Messages in queue until empty agin. t
            ESP_LOGV(TAG_WS, "--  Processing Log-Messages-queue with %d messages", areMsgsInQueue);
            while (uxQueueMessagesWaiting(log_xQueue))  // Do this until queue is empty   
            {   BaseType_t feedback = xQueueReceive(log_xQueue, &receivedMsg, QUEUE_WAIT_MS); // Receive the message from the queue
                if (feedback == pdPASS)
                { // Got message >> Process it
                  char sse_buf[MAX_MSG_SIZE+50];                                        // Buffer to hold the SSE message, +50 for sse-overhead
                  snprintf(sse_buf, sizeof(sse_buf), "data: %s\n\n", receivedMsg);      // Format the message for SSE
                  esp_err_t ret = httpd_resp_send_chunk(req, sse_buf, strlen(sse_buf)); // Send the message to the client
                  if (ret != ESP_OK) {
                      switch (ret) {
                        case ESP_ERR_HTTPD_RESP_SEND:
                            ESP_LOGD(TAG_WS,"⚠️ Error sending response packet: Err-Num= %d, Name= %s", ret, esp_err_to_name(ret));
                            return ret; // Exit handler on error
                            break;
                        default:
                            ESP_LOGW(TAG_WS, "❌ Could not send Messege from queue to Webserial-Log: Client disconnected or send error: %s", esp_err_to_name(ret));
                            /*-------------------------------------------------------------------------------------------- 
                              HINT: This is a cruial error, appears when using VPN. Loss of connection to the WebServer.
                                    A REBBOOT is needed to restore the connection.
                            ---------------------------------------------------------------------------------------------*/
                            lossOfWebserverConnection = true; // Set the flag to indicate loss of connection
                            return ret; // Exit handler on error
                          break;
                      }
                  }
                }
                vTaskDelay(pdMS_TO_TICKS(10)); // Relax time for the WebServer to process other requests
            }
            ESP_LOGV(TAG_WS, "DONE Processing >> Log-Messages-queue is emptied");
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // Relax time for the WebServer to process other requests
    }
    // ++++++++++++++++++++++++++++++++++  END Of EndLESS LOOP  ++++++++++++++++++++++++++++++++++
    return ESP_OK;
}

/*================================================================================
  Handle_WebServer_ESP_Reboot_GET() : 
    * This function handles the GET request for the ESP Reboot.
  used by: start_logwebserver() 
=================================================================================*/
esp_err_t Handle_WebServer_ESP_Reboot_GET(httpd_req_t *req)
{   vTaskDelete(modbus_poll_task_handle); // Delete the Modbus Polling Task to stop it
    vTaskDelete(mqtt_publish_task_handle_PRM); // Delete the MQTT Publish Task to stop it
    //--------------------------------------
    // Store the lastBootReason to NVS
    //--------------------------------------
    if (isNVSready) { // Check if NVS is ready
        esp_err_t err = Helper_store_str_to_nvs("lastBootReason", "Manual reboot via WebSerial"); // Store the lastBootReason to NVS
        if (err != ESP_OK) { // Check if the store was successful
            ESP_LOGE(TAG, "--     ❌ Failed store 'lastBootReason' in NVS: %s", esp_err_to_name(err)); // Log the error
        } else {  
            ESP_LOGD(TAG, "--     ✅ Stored 'lastBootReason' in NVS: Manual reboot via WebSerial"); // Log the success
        }          
    }
    //--------------------------------------
    // Do reboot with countdown
    //--------------------------------------       
    Helper_Reboot_with_countdown(TAG_WS); // 3s Countdown, then rebooting
    return ESP_OK;
}

/*================================================================================
  Handle_WebServer_ESP_OTA_GET() : 
    * This function handles the GET request for the ESP Reboot.
  used by: start_logwebserver() 
=================================================================================*/
esp_err_t Handle_WebServer_ESP_OTA_GET(httpd_req_t *req)
{   ESP_LOGE(TAG_WS, "--  🚨 Check for OTA Update is triggered.");
    start_ota_task(); // Start the OTA task to check for updates (if not already running)  
    return ESP_OK;
}

/*================================================================================
  report_open_web_socket_fn() : This function is called when a new socket is opened.
    * It increments the open socket counter and logs the event.
  used by: start_PowerMeter_WebServer()
=================================================================================*/
esp_err_t report_open_web_socket_fn(httpd_handle_t hd, int sockfd) {
    openSocketCounter++; // Increment the open socket counter
    ESP_LOGD(TAG_WS, "🟢  OPEN  WS-Socket: %d - Open-Sockets: %d", sockfd, openSocketCounter);
    return ESP_OK;}
/*================================================================================
  report_close_web_socket_fn() : This function is called when a socket is closed.
    * It decrements the open socket counter and logs the event.
  used by: start_PowerMeter_WebServer()
=================================================================================*/
void report_close_web_socket_fn(httpd_handle_t hd, int sockfd) {
    openSocketCounter--; // Decrement the open socket counter
    ESP_LOGD(TAG_WS, "🟥  CLOSE WS-Socket: %d - Open-Sockets: %d", sockfd, openSocketCounter);}

/*================================================================================
  Handle_WebServer_Icon_GET: Provide the inital Index-Page
  used by: start_PowerMeter_WebServer
=================================================================================
static esp_err_t Handle_WebServer_Icon_GET(httpd_req_t *req)
{   ESP_LOGI(TAG_WS, "--  Icon -> Deliver...");
    //-----------------------------------------------
    // Open File from LittleFS
    //-----------------------------------------------
    FILE *f = fopen("/rt_files/energy-meter-icon.ico", "r"); // Read the WebPage from LittleFS-Drive
    if (!f) {  // Check if the file was opened successfully
      ESP_LOGE(TAG_WS, "--  ❌ Failed to read 'energy-meter-icon.ico' from SPIFFS.bin."); 
      httpd_resp_send_404(req); return ESP_FAIL; }
    //-----------------------------------------------
    // Delver Icon
    //-----------------------------------------------
    httpd_resp_set_type(req, "image/x-icon"); // Set the response type to image/x-icon
    size_t buf_size = 1536; char *buf = malloc(buf_size); // Allocate a buffer to hold the file data
    size_t read_bytes;                      
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) { // Read file in chunks
       httpd_resp_send_chunk(req, buf, read_bytes);            // Send chunk to client
    }
    free(buf);                                                 // Free the buffer
    fclose(f);                                                 // Close the file
    httpd_resp_send_chunk(req, NULL, 0);                       // End response
    return ESP_OK;    
}
*/

/*================================================================================
  start_PowerMeter_WebServer: Starts WebServer & Create URI handlers
  Anser: Handle_to_WebServer
  used by: app_main 
=================================================================================*/
static httpd_handle_t start_PowerMeter_WebServer(void) 
{   // Start the async request workers needed for the WebServer 
    esp_err_t err = ESP_OK; // Set default error code
    // Settings for the WebServer as daemnon
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true; /* Enable Least Recently Used (LRU) purge mechanism
                                       Means: if Server runs out of sockets (too many requests), it will automatically close the
                                              least recent used connection to free up resources for new connections.
                                              This helps keep the server responsive and prevents it from getting stuck when all sockets are occupied.*/
    config.max_open_sockets = 20; // This is the maxium allowed open sockets
    config.max_uri_handlers = 10; // Maximum number of URI handlers
    config.recv_wait_timeout = 2; // Timeout s receiving data on a socket of HTTP server.
    config.send_wait_timeout = 1; // Timeout s for sending data
    config.open_fn = &report_open_web_socket_fn; // Pointer to a function that will be called when a new socket is opened
    config.close_fn = &report_close_web_socket_fn; // Pointer to a function that will be called when a socket is closed
    // Start the httpd server
        ESP_LOGI(TAG_WS, "--     * WebServer Listen to port:       '%d'", config.server_port);
    // Start the httpd server  (d=deamon)  
    err = httpd_start(&handle_to_WebServer, &config) ; 
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "!!     ⚠️ Failed to Start WebServer: Turn Logging on 'esp_log_level_set()' to see more details");
        return NULL;
    } else { 
        ESP_LOGI(TAG, "--     ✅ WebServer is up and running with IP-Address:           http://%s", get_lan_ip_info());
        ESP_LOGI(TAG, "--                                                 or:           %s", url_with_hostname);
    }
    //................................................................
    // Create URI handlers structs
    //................................................................
/*
    //----------------------------------------------------------------
    // Icon                                             "/favicon.ico"
    //----------------------------------------------------------------    
    const httpd_uri_t icon_uri = {
        .uri       = "/favicon.ico",
        .method  = HTTP_GET, .handler = Handle_WebServer_Icon_GET};
    err = httpd_register_uri_handler(handle_to_WebServer, &icon_uri);      
     if (err != ESP_OK) { ESP_LOGE(TAG_WS, "!! ⚠️ Error registering update Icon handler: %s",icon_uri.uri); return NULL; }
    else { ESP_LOGI(TAG_WS, "--     * Registered handler for URI:     %s", icon_uri.uri);}
*/        
    //----------------------------------------------------------------
    // Register handler for the Powermeter-Index-Page         "/" page
    //----------------------------------------------------------------
    const httpd_uri_t indexPage_uri = {
        .uri    = "/",      .method  = HTTP_GET, .handler = Handle_WebServer_SDM_Index_GET};
    err = httpd_register_uri_handler(handle_to_WebServer, &indexPage_uri);
    if (err != ESP_OK) { ESP_LOGE(TAG_WS, "!! ⚠️ Error registering Index-Page handler: %s",indexPage_uri.uri); return NULL; }
    else { ESP_LOGI(TAG_WS, "--     * Registered handler for URI:     %s", indexPage_uri.uri);}
    //----------------------------------------------------------------
    // Register handler for update Powermeter-XML with values   "/xml"
    //----------------------------------------------------------------        
    const httpd_uri_t xml_put_uri = {
        .uri  = "/xml",      .method = HTTP_PUT, .handler = Handle_WebServer_SDM_Values_PUT};
    err = httpd_register_uri_handler(handle_to_WebServer, &xml_put_uri);
    if (err != ESP_OK) { ESP_LOGE(TAG_WS, "!! ⚠️ Error registering XLM update handler: %s",xml_put_uri.uri); return NULL; }
    else { ESP_LOGI(TAG_WS, "--     * Registered handler for URI:     %s", xml_put_uri.uri);}
    //----------------------------------------------------------------
    // Register handler for the Logging-Index-Page         "/webserial"
    //----------------------------------------------------------------
    const httpd_uri_t logPage_uri = {
        .uri = "/webserial", .method  = HTTP_GET, .handler = Handle_WebServer_Logging_Index_GET};
    err = httpd_register_uri_handler(handle_to_WebServer, &logPage_uri);
    if (err != ESP_OK) { ESP_LOGE(TAG_WS, "!! ⚠️ Error registering Index Page handler: %s",logPage_uri.uri); return NULL; }
    else { ESP_LOGI(TAG_WS, "--     * Registered handler for URI:     %s", logPage_uri.uri);}
    //----------------------------------------------------------------
    // Register handler for update the Log-Messages          "/events"
    //----------------------------------------------------------------
    const httpd_uri_t sse_uri = {
        .uri       = "/events",  .method  = HTTP_GET, .handler = Handle_WebServer_Logging_ServerSentEvents_GET};
    err = httpd_register_uri_handler(handle_to_WebServer, &sse_uri);    
    if (err != ESP_OK) { ESP_LOGE(TAG_WS, "!! ⚠️ Error registering update Logs handler: %s",sse_uri.uri); return NULL; }
    else { ESP_LOGI(TAG_WS, "--     * Registered handler for URI:     %s", sse_uri.uri);}
    //----------------------------------------------------------------
    // Register handler for REBOOT (Buttion)                 "/reboot"
    //----------------------------------------------------------------
    const httpd_uri_t reboot_uri = {
        .uri       = "/reboot",  .method  = HTTP_GET, .handler = Handle_WebServer_ESP_Reboot_GET};
    err = httpd_register_uri_handler(handle_to_WebServer, &reboot_uri);    
    if (err != ESP_OK) { ESP_LOGE(TAG_WS, "!! ⚠️ Error registering update Logs handler: %s",reboot_uri.uri); return NULL; }
    else { ESP_LOGI(TAG_WS, "--     * Registered handler for URI:     %s", reboot_uri.uri);} 
    //----------------------------------------------------------------
    // Register handler to check for OTA-Update (Button)        "/ota"
    //----------------------------------------------------------------    
    const httpd_uri_t ota_uri = {
        .uri       = "/ota",     .method  = HTTP_GET, .handler = Handle_WebServer_ESP_OTA_GET};
    err = httpd_register_uri_handler(handle_to_WebServer, &ota_uri);      
     if (err != ESP_OK) { ESP_LOGE(TAG_WS, "!! ⚠️ Error registering update Logs handler: %s",ota_uri.uri); return NULL; }
    else { ESP_LOGI(TAG_WS, "--     * Registered handler for URI:     %s", ota_uri.uri);}
    //----------------------------------------------------------------
    return handle_to_WebServer;        
}
/*================================================================================
  stop_PowerMeter_WebServer: Stop WebServer
  Anser: Error code
  used by: Handle_TCPIP_Disconnect & app_main
=================================================================================*/
static esp_err_t stop_PowerMeter_WebServer(httpd_handle_t handle_to_WebServer)
{ return httpd_stop(handle_to_WebServer); // Stop the httpd server
}

/*#################################################################################################################################
    PING GATEWAY FAIL    PING GATEWAY FAIL    PING GATEWAY FAIL    PING GATEWAY FAIL    PING GATEWAY FAIL    PING GATEWAY FAIL   
##################################################################################################################################*/
#ifdef CONFIG_XLAN_USE_PING_GATEWAY
/** ------------------------------------------------------------------------------------------------
 * @brief  TASK-Handler to frequently check if the Gateway was reachable, if not REBOOT the ESP
 * 
 * 
 * @note
 *    used by `app_main`
 *  -----------------------------------------------------------------------------------------------*/
void Task_ping_gateway_fail_reboot(void *arg)
{ while (1) { // Infinite loop of this task
      //------------------------------------------
      // ENDLESS LOOP
      //------------------------------------------
      // Start with waiting for the next cycle
      //------------------------------------------
      vTaskDelay(pdMS_TO_TICKS( CONFIG_XLAN_PING_GATEWAY_INTERVAL_SEC * (1000) ));
       //------------------------------------------
      // Check if the Gateway was NOT reachable
      //------------------------------------------     
      if (!was_last_ping_successful())
      { // If the last ping was successful, do nothing
          ESP_LOGE(TAG, "--  ❌ Gateway was NOT reachable, rebooting ESP32"); // Log the error
          //--------------------------------------
          // Store the lastBootReason to NVS
          //--------------------------------------
          if (isNVSready) { // Check if NVS is ready
              esp_err_t err = Helper_store_str_to_nvs("lastBootReason", "Gateway unreachable"); // Store the lastBootReason to NVS
              if (err != ESP_OK) { // Check if the store was successful
                  ESP_LOGE(TAG, "--     ❌ Failed store 'lastBootReason' in NVS: %s", esp_err_to_name(err)); // Log the error
              } else {  
                  ESP_LOGD(TAG, "--     ✅ Stored 'lastBootReason' in NVS: Gateway unreachable"); // Log the success
              }            
          }
          //--------------------------------------
          // Do reboot with countdown
          //--------------------------------------
          Helper_Reboot_with_countdown(TAG); // 3s Countdown, then rebooting
      }
  } // End of the infinite loop 
} // END of the Task-Function  
#endif // CONFIG_XLAN_USE_PING_GATEWAY

/*#################################################################################################################################
  TCPIP   ETHERNET   TCPIP   ETHERNET   TCPIP   ETHERNET   TCPIP   ETHERNETTCPIP   ETHERNET   TCPIP   ETHERNET   TCPIP   ETHERNET
##################################################################################################################################*/

/*================================================================================
  Handle_TCPIP_Disconnect: Handler when connection GOES DOWN
            Stops WebServer when 'Lost-TCPIP'-Event happens.
  used by: app_main
=================================================================================*/
static void Handle_TCPIP_Disconnect(void* handle_of_TCPIP, esp_event_base_t event_base, int32_t event_id, void* event_data)
{   // Connect/Glue the WebServer to the TCPIP event
    //httpd_handle_t* handle_to_WebServer = (httpd_handle_t*) handle_of_TCPIP;
    if (handle_to_WebServer) {
        ESP_LOGD(TAG, "--  Stopping WebServer");
        if (stop_PowerMeter_WebServer(handle_to_WebServer) == ESP_OK) { handle_to_WebServer = NULL;}
        else { ESP_LOGE(TAG, "Failed to stop http server");}}
}
/*================================================================================
  Handle_TCPIP_Connect: Handler when connection GOES UP
            Starts WebServer when 'GOT-IP'-Event happens.
  used by: app_main
=================================================================================*/
static void Handle_TCPIP_Connect(void* handle_of_TCPIP, esp_event_base_t event_base, int32_t event_id, void* event_data)
{   //httpd_handle_t* handle_to_WebServer = (httpd_handle_t*) handle_of_TCPIP;
    if (handle_to_WebServer == NULL) {
        ESP_LOGD(TAG, "--  Starting WebServer (triggered by GOT-IP-Event)");   
        handle_to_WebServer = start_PowerMeter_WebServer();
    }
}

/*#################################################################################################################################
     MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN   MAIN
##################################################################################################################################*/
void app_main(void)
{   esp_err_t err= ESP_OK; // Set default error code     
    str_Error_RegisterRead= esp_err_to_name(ESP_OK); ;  // Init the error message
    /*---------------------------------------------
      D. Set the your wished log levels
    ----------------------------------------------*/
    /* ESP_LOG_NONE <None>  -- ESP_LOG_ERROR <Errors> -- ESP_LOG_WARN <Warnings> 
       ESP_LOG_INFO <Info>  -- ESP_LOG_DEBUG <Debug>  -- ESP_LOG_VERBOSE <Verbose>  -- ESP_LOG_VERBOSE*/
    esp_log_level_set(TAG, CONFIG_PRM_MAIN_LOG_LEVEL);  // Log level for main
 #ifdef CONFIG_PRM_MAIN_WEBSERIAL_USE
    ESP_LOGI(TAG, "###################################################################################");
    /*----------------------------------------------------------
      X. Change ESPLOGx Output-Target to WebServer (additional)
    ----------------------------------------------------------*/
    // Crate a static queue capable of holding 20 a_fmted_ESPLOGx_Message elements
    log_xQueue = xQueueCreate(
        QUEUE_LENGTH,                    // Number of elements to hold max, then old ones are dropped  <uxQueueLength> 
      sizeof(a_fmted_ESPLOGx_Message));  // // Create a queue // Size per Queue Data-Element to hold                        <uxItemSize>
    // Change the log output target to the WebServer & Keep original handler in 'orig_log_handler' to use in parallel
    orig_log_handler = esp_log_set_vprintf(Handle_ESPLOGx_custom); // Set the custom log handler
#endif
    /*--------------------------------------------------------------------------
      0. Show INFORMATION about the running FIRMWARE
    ---------------------------------------------------------------------------*/
    const esp_app_desc_t* app_desc = esp_app_get_description(); // Get the app description set
    // Get & store global variables
    project_name = app_desc->project_name;   // From inject in CMakeList.txt            : Firmware-Name = Project-Name
    firmware_version = app_desc->version;    // From version.txt in root of the project : Firmware-Version
                                             // From point in time of build:            : Firmware-Build-Timestamp
    snprintf(firmware_build_ts, sizeof(firmware_build_ts), "%s @ %s", app_desc->date, app_desc->time); // See the formating
    ESP_LOGI(TAG, "###################################################################################");
    ESP_LOGI(TAG, "--  What firmeware is running?"); 
    ESP_LOGI(TAG, "------------------------------"); 
    ESP_LOGI(TAG, "--  Project:      %s", project_name);           // From inject in CMakeList.txt  
    ESP_LOGI(TAG, "--  Source-Path:  %s", PRJ_PATH);               // From inject in CMakeList.txt 
    ESP_LOGI(TAG, "--  Version:      %s (set in version.txt @ root-folder)",
                                          firmware_version);       // from version.txt in root of the project
    ESP_LOGI(TAG, "--  Build time:   %s", firmware_build_ts);      // Last build Data & Time
    ESP_LOGI(TAG, "--  Chip-Name:    %s", CONFIG_IDF_TARGET);      // Chip name from menuconfig                             
    ESP_LOGI(TAG, "###################################################################################");
    ESP_LOGI(TAG, "--  Start 'app_main' function");
    //---------------------------------------------------
    // ADD-ON from above 1. Change ESPLOGx - LOGGING HERE
    //---------------------------------------------------
 #ifdef CONFIG_PRM_MAIN_WEBSERIAL_USE    
                         ESP_LOGI(TAG, "--  1. Add the ESPLOGx-Log in addtion to WebServer");
    // Check if both commands were successful
    if (log_xQueue == NULL || orig_log_handler == NULL) {
    ESP_LOGE(TAG, "!!     ⚠️ Failed to set up forward of ESPLOGx to WebServer");
    } else {
    ESP_LOGI(TAG, "--     ✅ Establish: ESPLOGx forward to WebServer (in addtion).");};
  #endif
    /*--------------------------------------------------------------------------
      2. Init LittleFS (at Flash) to store the HTML pages 
    ---------------------------------------------------------------------------*/
                        ESP_LOGI(TAG, "--  2. Mount LittleFS Partition to use files at runtime...");    
    esp_vfs_littlefs_conf_t runtimeFS_config = {
        .base_path =        "/rt_files",  // Root-Path in FS = Mounting point
        .partition_label =  "storage",    // Partition label
        .format_if_mount_failed = false  // NO formating when mount fails
    };
    err = esp_vfs_littlefs_register(&runtimeFS_config);
    if (err != ESP_OK) {ESP_LOGE(TAG, "!!     ❌ Failed to mount storage.bin.");
                        ESP_LOGE(TAG, "!!        * Do you use a custom partition table? Must be activated with menuconfig.");
                        ESP_LOGE(TAG, "!!        > 'Partion Table' > 'Custom partition table CSV' // The name is 'partitions.csv'");
                        ESP_LOGE(TAG, "!!        * You need at least 4MByte of RAM, dont forget to set this with menuconfig.");
    } else {
      // Get the LittleFS size infos
      size_t total = 0, used = 0;  err = esp_littlefs_info(runtimeFS_config.partition_label, &total, &used);
      if (err == ESP_OK) {
                        ESP_LOGI(TAG, "--     ✅ storage.bin is mounted, ready for file access. (Uses %d of %dkByte).",  (int)(used / 1024), (int)(total / 1024));
      } else {          ESP_LOGE(TAG, "!!     ❌ Mounted, BUT failed to get LittleFS-Infos. Error= (%s)", esp_err_to_name(err));}
    } 
    /*--------------------------------------------------------------------------
      3. Establish connection to LAN with Ethernet
    ---------------------------------------------------------------------------*/  
                        ESP_LOGI(TAG, "--  3. Establish connection to LAN. Take a while...");
    err = connect_to_xlan();  // ERROR Check is missing!   //    ESP_ERROR_CHECK(example_connect());
    if (err != ESP_OK) {ESP_LOGE(TAG, "!!     ⚠️ Failed to connect to LAN: Turn Logging on 'esp_log_level_set()' to see more details"); return; // Exit the function if connection fails
    } else {            ESP_LOGI(TAG, "--     ✅ LAN Connection ESTABLISHED                             http://%s", get_lan_ip_info());}; 
    /*--------------------------------------------------------------------------
      4. Get the Time form NTP-Server & set local TZ
    ---------------------------------------------------------------------------*/
                        ESP_LOGI(TAG, "--  4. Get the Time form NTP-Server & set local TZ. Take a while...");
    if (is_lan_connected())
    {   err = SyncNTP_and_set_LocalTZ(); // SyncNTP and set local TZ
        if (err != ESP_OK) {ESP_LOGE(TAG, "!!     ⚠️ Failed to receive Time from Network Time Protocol, Turn Logging on 'esp_log_level_set()' to see more details");
        } else {            ESP_LOGI(TAG, "--     ✅ NTP-Sync and local TZ SET");};
        getShortTimesStamp(s_ts, sizeof(s_ts));
                            ESP_LOGI(TAG, "--     → Current local time is:                                  %s", s_ts);// Log the current time
    } else {                ESP_LOGW(TAG, "--     ⚠️ LAN is NOT connected, so can NOT get the time from NTP-Server"); } 
    /*--------------------------------------------------------------------------
      5. Establish WebServer
    ---------------------------------------------------------------------------*/
                        ESP_LOGI(TAG, "--  5. Establish WebServer to show the measured Powermeter values");
    esp_log_level_set(TAG_WS,     CONFIG_PRM_WEBSERVER_LOG_LEVEL);  //  _WEB_SVR:  Log-Level for Webserver (async)
    esp_log_level_set("httpd",    CONFIG_PRM_HTTPDAEMON_LOG_LEVEL);  //  httpd:     Log-Level for ESP-IDF HTTPD
    start_async_req_workers(); // Start the async request workers needed for one part the WebServer   
    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected, and re-start it upon connection. */
    /* WebServer will be started & stopped with the following Ethenet handler
       >> Register event handler for 'esp_event_loop_create_default'
       >> OVERWIRTE allready established(created) handlers in Connect_to_LAN()  */
    ESP_ERROR_CHECK(esp_event_handler_register(
                      IP_EVENT,                    // Event-Type: IP-Event
                      IP_EVENT_ETH_GOT_IP,         // Default-Event: Got IP
                      &Handle_TCPIP_Connect,       // The handler that will be called
                      handle_to_WebServer));       // Start webserver when ethernet is connected check's is server is allready onnline
    ESP_ERROR_CHECK(esp_event_handler_register(
                      ETH_EVENT,                   // Event-Type: Ethernet-Event
                      ETHERNET_EVENT_DISCONNECTED, // Default-Event: Ethenet dissconnected
                      &Handle_TCPIP_Disconnect,    // The handler that will be called 
                      handle_to_WebServer)); // Stop webserver when ethernet is disconnected
    // 'Got IP' event if allready over to start the WebServer manually if needed
    if ( handle_to_WebServer==NULL && is_lan_connected() ) {
        handle_to_WebServer = start_PowerMeter_WebServer();
    }
    /*--------------------------------------------------------------------------
      6. Establish Modbus RTU Connection
    ---------------------------------------------------------------------------*/
                        ESP_LOGI(TAG, "--  6. Establish Modbus RTU Connection to Powermeter to read Registers");
    esp_log_level_set(TAG_MB_READ, CONFIG_PRM_MODBUS_LOG_LEVEL);  //  MB_R_REG:  Log-Level for frequent read of Modbus Registers
    esp_log_level_set(TAG_MB_PUBL, CONFIG_PRM_MODBUS_LOG_LEVEL);  //  MQ_P_REG:  Log-Level for frequent read of Modbus Registers
    Modbus_Build_ParaDescriptors_PowerMeter();    // Build The Parameter-Descriptors for MB-Controller 
    err= Start_Modbus_RTU_Workers(
                &ptr_Handle_to_Modbus_MasterController,  // If Start was successful, the Handle to Modbus-Controller is RETURNED
                powermeter_param_descriptors,                   // Pointer to the SDM Device Register >> Parameter DESCRIPTOR Table
                MBREG                                    // Number of descriptors in the table
    );           
    ESP_ERROR_CHECK(err); // Check for errors
    // Create the FreeRTOS task to poll the SDM registers
    xTaskCreate(Task_Modbus_SDM_Poll_RegisterValues, "Task_Modbus_SDM_Poll_RegisterValues", 4096, NULL, 4, &modbus_poll_task_handle);
    /*--------------------------------------------------------------------------
      7. OTA - Enable Over-The-Air Update (!after IP connection ist established) 
    ---------------------------------------------------------------------------*/
                        ESP_LOGI(TAG, "--  7. Check for Over-The-Air Update (OTA)");
     // Start OTA to check for updates (at least one then booting >> depending on the menuconfig)
    start_ota_task(); // Start the OTA task
    /*--------------------------------------------------------------------------
      8. Initialize NVS to store the last boot reason
    ---------------------------------------------------------------------------*/
    ESP_LOGI(TAG, "--  8. Initialize NVS & get last boot reason");
    err = nvs_flash_init(); // Initialize NVS Flash
    char *string_lastBootReason = NULL; // Variable to hold the last boot reason
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "--     ❌ NVS Flash initialization failed: %s", esp_err_to_name(err));
      isNVSready = false; // Set the flag to indicate NVS is not ready
    } else {
      isNVSready = true; // Set the flag to indicate NVS is ready
      string_lastBootReason = Helper_read_from_nvs_with_key("lastBootReason"); // Read the NVS with the key
    } 
    /*--------------------------------------------------------------------------
      9. Establish connection to my MQTT
    ---------------------------------------------------------------------------*/
                        ESP_LOGI(TAG, "--  9. Establish connection to my MQTT Broker");
    esp_log_level_set(TAG_ESP_PUBL, CONFIG_PRM_MQTT_LOG_LEVEL);//  MQ_P_ESP:  Log-Level for publish of ESP-Values
    esp_log_level_set(TAG_COM_PUBL, CONFIG_PRM_MQTT_LOG_LEVEL);//  MQ_P_ESP:  Log-Level for publish of Common Infos
    getShortTimesStamp(s_ts, sizeof(s_ts));
    err = Start_myMQTT_Client(&handle_to_MQTT_client, s_ts);
    if (err == ESP_OK) {ESP_LOGI(TAG, "--     ✅ Connection to Broker ESTABLISHED.");
      } else {          ESP_LOGE(TAG, "!!     ⚠️ Failed to connect: Turn Logging on 'esp_log_level_set()' to see more details");  }
    ESP_ERROR_CHECK(err); // Check for errors
    // Create the FreeRTOS task to publish the MQTT messages grabbed from Modbus Powermeter
    xTaskCreate(Task_MQTT_PowerMeter_Publish, "Task_MQTT_PowerMeter_Publish", 4096, NULL, 5, &mqtt_publish_task_handle_PRM);
    // Create the FreeRTOS task to publish ESP's free heap frequently as MQTT messages. HINT: It appears like Powermeter-value
    xTaskCreate(Task_MQTT_publish_ESP_freeHeap, "Task_MQTT_publish_ESP_freeHeap", 3072, NULL, 6, NULL);
    if (is_mqtt_connected()) // Only if MQTT-Broker is connected
    { // Publish the common ESP infos to MQTT
      MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"Last-Boot-Time", s_ts);                 // Last-Boot-Time
      MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"Project-Name", project_name);           // Project-Name
      MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"Source-Path", PRJ_PATH);                // Source-Path
      MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"Firmware-Version", firmware_version);   // Firmware-Version 
      MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"Build-Time", firmware_build_ts);        // Firmware-Build-time
      MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"Chip-Name", CONFIG_IDF_TARGET);         // ESP Chip/Target-Name
      MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"Hostname at LAN", CONFIG_XLAN_HOSTNAME);// ESP's Hostname
      MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"mDNS-URL", url_with_hostname);          // ESP's mDNS URL
      MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"IP-Address", get_lan_ip_info());        // ESP's IP-Address
      MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"OTA-URL", get_ota_url());               // ESP's OTA URL
      // Publish as 'Measurement' to be shown with tools like Grafana
      MQTT_Publish_OneTime_Measure(MQTT_MEASUREMENT_SUB_TOPIC,"ESP-LastBootTime",s_ts);     // Last-Boot-Time as Measurement
      if (string_lastBootReason != NULL) {  // if received 
        MQTT_Publish_OneTime_Measure(MQTT_MEASUREMENT_SUB_TOPIC,"ESP-LastBootReason",string_lastBootReason);
        MQTT_publish_Common_infos(MQTT_ESP_SUB_TOPIC,"Last-Boot-Reason", string_lastBootReason); 
        free(string_lastBootReason); // Free the allocated memory for the string
      }
      // Publish the Powermeter name to MQTT
      MQTT_publish_Common_infos(MQTT_PRM_SUB_TOPIC,"Powermeter-Device", PRM_Name);          // Powermeter-name 
    }
#ifdef CONFIG_XLAN_USE_PING_GATEWAY
    /*---------------------------------------------------------------------------------
      10. Establish a TASKs to check if Webserver might not reachable
         (a) Task: Check if Gateway-Ping is successful
                   uses: was_last_ping_successful() from component xlan_connection
         (b) Task: Check if httd throws an error on connection loss 
                   this happend when accessed via VPN 
                   uses: flag 'lossOfWebserverConnection' to indicate connection loss
                               to grab HTTP-Daemon error there is an hook on ESP_LOGx
      A reboot is triggered if one of the checks fails.
    ----------------------------------------------------------------------------------*/
    ESP_LOGI(TAG, "-- 10. Establish a TASKs to check if Webserver is reachable, if not reboot ESP!");
    xTaskCreate(Task_ping_gateway_fail_reboot, "Task_Reboot_if_GW_ping_failed", 3072 /* 3kb */, NULL, 6, NULL);
    ESP_LOGI(TAG, "--     (a) Task to check if Gateway-Ping is successful every %d sec", CONFIG_XLAN_PING_GATEWAY_INTERVAL_SEC);
    xTaskCreate(Task_is_webserver_connection_loss_then_reboot, "Task_Reboot_if_WebS_conn_loss", 20248 /* 2kb */, NULL, 6, NULL);
    ESP_LOGI(TAG, "--     (b) Task to check on curial error on HTTP-Daemon connection loss (happend on VPN usage) every 60 sec.");
#endif // CONFIG_XLAN_USE_PING_GATEWAY

   /*--------------------------------------------------------------------------
      X. WAIT
    ---------------------------------------------------------------------------*/
    // Wait 10 sec
    //ESP_LOGI(TAG, "###########  WAIT  WAIT  WAIT  WAIT  WAIT  WAIT  WAIT  WAIT >> 10sec...");
    //vTaskDelay(10000 / portTICK_PERIOD_MS);
    /*--------------------------------------------------------------------------
      E. END of the main function
    ---------------------------------------------------------------------------*/
    ESP_LOGI(TAG, "--  ✅ DONE 'app_main': Now FreeRTOS- Taks & Events are running.");
    ESP_LOGI(TAG, "###################################################################################");
}