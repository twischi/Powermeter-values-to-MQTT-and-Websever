/*#################################################################################################################################
    OTA  -  Over the Air Update  --  OTA  -  Over the Air Update  --  OTA  -  Over the Air Update  --  OTA  -  Over the Air Update
##################################################################################################################################*/

/*--------------------------------------------------------- 
   INCLUDES for this component
*---------------------------------------------------------*/
#include "OTA_mDNS.h"           // for THIs (own component)
#include "freertos/FreeRTOS.h"  // For FreeRTOS functions (create tasks, delay, etc.)
/*--------------------------------------------------------- 
   Get GLOBAL variables from main.c
*---------------------------------------------------------*/
static TaskHandle_t ota_task_handle = NULL; // Global/static variable for the task handle
bool is_ota_update_ongoing = false; // Global variable to indicate if an OTA update is ongoing
extern const char *project_name;
/* If not set in your main you need to fill it yourself
   What is is for?
   ---------------
   The char* is used to create the OTA-URL wating to
   come availilbe to update via OTA
*/

/*--------------------------------------------------------- 
  ESP Logging: TAG of this component
*---------------------------------------------------------*/
#include <esp_log.h>          // For ESP-logging
#define TAG_OTA "🚨OTA_Upd"   // TAG for logging
/*--------------------------------------------------------- 
  Needed includes and VARIABLES for OTA updates
*---------------------------------------------------------*/
#include "esp_ota_ops.h"      // For OTA operations
#include "esp_https_ota.h"    // For OTA updates using HTTPS 
#include "esp_http_client.h"  // During OTA needs to be a HTTP client
char ota_url[128];          // Url for the OTA update, will created at runtime 

/*---------------------------------------------------------------- 
  CONFIGURATION from menuconfig - As interface for easy debugging
*----------------------------------------------------------------*/
//#include "sdkconfig.h"
#ifdef CONFIG_OTA_MANUALLY_TRIGGER_ONLY
    bool isManuallyTriggerOnly = true; // If true, the OTA update is triggered automatically
    #define OTA_CHECK_INTERVAL_MS  (10000)          // Set DUMMY to be save
#else
    #define OTA_CHECK_INTERVAL_MS  (CONFIG_OTA_CHECK_INTERVAL_SECS * 1000) // Interval to check for OTA updates in milliseconds
    bool isManuallyTriggerOnly = false; // If false, the OTA update is triggered mannually
#endif

/*================================================================================
  ota_sanitize_hostname(): Sanitize the hostname by replacing invalid characters
  Anser: none, but modifies the hostname in place
  used by: Task_do_OTA() 
=================================================================================*/
void ota_sanitize_hostname(char *hostname) { 
    for (char *p = hostname; *p; ++p) { // Allowed characters: a-z, A-Z, 0-9, '-', '.', ':', '/'
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || // Allowed a-z, A-Z, 0-9              
               (*p == '-') || (*p == '.') || (*p == ':') || (*p == '/') ))                         // Special characters allowed
        {  // ALL other characters are invalid  
            *p = '-'; // Replace invalid char with hyphen
        }
    }
}

/*================================================================================
  check_firmware_available: Check if the firmware is available at the given URL 
  Anser: true if available, false if not
  used by: ??? 
=================================================================================*/
bool check_firmware_available(const char *url)
{   esp_err_t err; int status;
    esp_http_client_handle_t handle_http_client; // Handle for the HTTP client
    //--------------------------------------------------------------
    // HTTP client configuration for checking firmware availability
    //--------------------------------------------------------------
    esp_http_client_config_t http_client_config = {
        .url = url,
        .method = HTTP_METHOD_HEAD,  // HEAD ist leichtgewichtig
        .timeout_ms = 3000,
    };
    //--------------------------------------------------------------
    // Just do a HEAD request to check if the firmware is available
    //--------------------------------------------------------------
    handle_http_client = esp_http_client_init(&http_client_config); // Receive a handle to the HTTP client
    if (handle_http_client == NULL) { ESP_LOGW(TAG_OTA, "❌❌❌ Failed to init HTTP client url=%s", http_client_config.url); return false; }
    err = esp_http_client_perform(handle_http_client); // Perform the HTTP request 
    status = esp_http_client_get_status_code(handle_http_client); // Get the HTTP status code from the response
    esp_http_client_cleanup(handle_http_client);
    //--------------------------------------------------------------
    // Process the response
    //--------------------------------------------------------------
    if (err == ESP_OK && status == 200) {
        ESP_LOGI(TAG_OTA, "Firmware available at %s", url);
        return true;
    } else {
        ESP_LOGV(TAG_OTA, "Firmware not available, HTTP status = %d", status);
        return false;
    }
}

/*================================================================================
  Task_do_OTA: This task performs an OTA update using the esp_https_ota function.
  used by: (TASK created by app_main)
=================================================================================*/
void Task_do_OTA(void *pvParameter)
{   esp_err_t err; // Error code for ESP operations
    //----------------------------------------------------------------------------
    // USED ⚠️ HTTP CLIENT CONFIGURATION
    //----------------------------------------------------------------------------
    // (1) Build the OTA URL dynamically from project name
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    if (project_name == NULL || project_name[0] == '\0') {  // Check if firmware_name is set, if not use a default name
        ESP_LOGW(TAG_OTA, "⚠️ Firmware name is not set, using default name");
        snprintf(ota_url, sizeof(ota_url), "http://firmware:8070/firmware.bin");
    } else {
        // BUILD the OTA URL dynamically from the project name
        // !! BE AWARE: this url needs mDNS to resolve the hostname on the local network / server-side 
        // !!           provindig the OTA binary file to this url 
        snprintf(ota_url, sizeof(ota_url), "http://%s.local:8070/OTA.bin", project_name);
    }
    // Sanitize the hostname in the URL make sure it only contains valid characters
    ota_sanitize_hostname(ota_url); // As the firmware_name might contain invalid characters for hostname 
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // (2) Config the HTTP client
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    esp_http_client_config_t http_config = {
        .url = ota_url,                   // URL to HTTP-Server providing the .bin-File for OTA
        .timeout_ms = 2000,               // Timeout for the HTTP request
        .disable_auto_redirect = false,}; // Enable automatic redirection
    ESP_LOGI(TAG_OTA,"      ℹ️ : OTA will wait for this ota_url=             %s",http_config.url);
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // (3) Config for OTA with usage of (2) 
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    esp_https_ota_config_t ota_config = { .http_config = &http_config, };
    // +++++++++++++++++++++++++++++++++++++  ENDLESS LOOP  +++++++++++++++++++++++++++++++++++++
    while (true) 
    {   //............................................................................
        // Check if the firmware is available at the given URL
        //............................................................................
        bool is_frmw_available = check_firmware_available(ota_url); 
        if (is_frmw_available) {
            //.......................................................................
            // RUN The OTA update
            //........................................................................
            ESP_LOGI(TAG_OTA,"⚠️ ⚠️ OTA Update is triggered! Starts NOW!");
            is_ota_update_ongoing = true; // Set Signal that an OTA update is ongoing
            err = esp_https_ota(&ota_config);
            if (err == ESP_OK) {
                ESP_LOGI(TAG_OTA,"✅ OTA successful. Rebooting to apply the update.");
                for (int i = 3; i > 0; i--) { // Show a countdown
                    ESP_LOGI(TAG_OTA,"🔔 in %d seconds... 🔔", i);
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 sec            
                }
                ESP_LOGI(TAG_OTA,"⛔️ ⛔️ REBOOT Wait: 10s for reload Webpages ⛔️ ⛔️");
                vTaskDelay(pdMS_TO_TICKS(500)); // Wait 1 sec
                esp_restart();
            } else {
                ESP_LOGE(TAG_OTA,"❌ OTA FAILED");
                is_ota_update_ongoing = false; // Reset Signal that an OTA update is ongoing
            }
        } else {
            ESP_LOGD(TAG_OTA,"❗️ Checked: NO updated firmware available @ %s", ota_url);
        }
        //.......................................................................
        // Distinguish between "Manually Trigger Only" and "Automatic OTA"
        //........................................................................        
        if (!isManuallyTriggerOnly) {
            // If not manually triggered, wait for the defined interval before checking again
            vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_MS)); // Wait for the defined interval before checking again
        } else {
            // MODE "Manually Trigger Only"
            ESP_LOGD(TAG_OTA,"❗️ MESSAGE: Stop OTA task after one-time check in 'Manually Trigger Only' mode.");
            ota_task_handle = NULL;
            vTaskDelete(NULL); // Delete this task when checked once
        }
    }
    // ++++++++++++++++++++++++++++++++++  END Of EndLESS LOOP  ++++++++++++++++++++++++++++++++++
}

/** ------------------------------------------------------------------------------------------------
 * @brief  Getter for the OTA URL
 * 
 * @return  char*  Returns a pointer to a static string containing the OTA URL.
 * 
 *  -----------------------------------------------------------------------------------------------*/
const char *get_ota_url(void) {
    return ota_url; // Return the OTA URL
}

/*================================================================================
  is_ota_update_in_progress(): Check if an OTA update is currently in progress
  used by: caller
=================================================================================*/
bool is_ota_update_in_progress(void) {
    // Check if an OTA update is currently in progress
    return is_ota_update_ongoing;
}   

/*================================================================================
  start_ota_task():
    Start the OTA task if it is not already running.
    This function creates the OTA task if it is not already created.
    If the task is already running, it logs a warning message.
  used by: caller
=================================================================================*/
void start_ota_task(void)
{   /*------------------------------------------------------------------------
      Set Log-Levels for this component
    ------------------------------------------------------------------------*/
    esp_log_level_set(TAG_OTA, CONFIG_OTA_LOG_LEVEL); // Set the log level for this component
    esp_log_level_set("esp-tls", ESP_LOG_NONE);         // Suppress logs
    esp_log_level_set("transport_base", ESP_LOG_NONE);  //  Suppress logs
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);     //  Suppress logs
    esp_log_level_set("esp_https_ota",ESP_LOG_ERROR);   // esp_https_ota: Log-Level for ESP-IDF HTTPD
    esp_log_level_set("🚨OTA_Upd", ESP_LOG_INFO);       // OTA_mDNS: Log-Level       
    // --------------------------------------------
    // Check if the OTA task is already running?
    //---------------------------------------------
    if (ota_task_handle == NULL) {
        //-----------------------------------------
        // NOT running >> so create the task
        //-----------------------------------------
        BaseType_t result = xTaskCreate(
            &Task_do_OTA,          // Task to do the OTA
            "ota_task",            // Name of the task
            8192,                  // Stack size in bytes
            NULL,                  // Parameters to pass to the task (NULL in this case)
            5,                     // Task priority (5 is a good default)
            &ota_task_handle       // Task handle it check if the task is already running
        );
        //-----------------------------------------
        // Check if the task was created successfully
        //-----------------------------------------
        if (result == pdPASS) {
            ESP_LOGD(TAG_OTA, "OTA task started.");
        } else {
            ESP_LOGE(TAG_OTA, "❌ Failed to start OTA task.");
            ota_task_handle = NULL;
        }
    } else {
        //-----------------------------------------
        // Already running >> log a warning
        //-----------------------------------------
        ESP_LOGD(TAG_OTA, "Info: OTA task already running, not need to start again.");
    }
}