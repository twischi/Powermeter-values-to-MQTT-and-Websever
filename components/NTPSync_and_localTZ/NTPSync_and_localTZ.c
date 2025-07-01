#include <stdio.h>
#include "NTPSync_and_localTZ.h"  // For THIS component

#include "esp_event.h"
#include "esp_netif_sntp.h"

/*------------------
  ESP Logging: TAG 
------------------*/
#include <esp_log.h>                // For ESP-logging
static const char *TAG = "sNTP_LTz"; // TAG for logging
/*                        12345678 */

/*############################################################################
  time_sync_notify_cb: Callback function > when time is synchronized
  used by: app_main
  Note: Called when the time is synchronized with the NTP server.
#############################################################################*/
void time_sync_notify_cb(struct timeval *tv)
{   ESP_LOGI(TAG, "    #########################################################");
    ESP_LOGI(TAG, "    # Notification: Received NTP time synchronization event #");
    ESP_LOGI(TAG, "    #########################################################");
}

/*------------------------------------------------------------
  Public FUNCTION of this component: Start_myMQTT_Workers
-------------------------------------------------------------*/

/*############################################################################
  getShortTimestampString():
      Calculate and give back as short readable char+ with latest-local time
  used by: caller
#############################################################################*/
char* getShortTimestampString()
{   struct tm timeinfo;  // Struct to hold the time information
    // Allocate memory for the string
    char* buffer = malloc(100); // Allocate enough space for the timestamp string
    if (buffer == NULL) {
        ESP_LOGE("getShortTimestampString", "Failed to allocate memory for timestamp string");
        return NULL; // Return NULL if memory allocation fails
    }
    time_t now = time(NULL); // Get the current time 
    localtime_r(&now, &timeinfo); // Convert the time to local time
    // Extract time components
    uint8_t second = timeinfo.tm_sec;       // Seconds after the minute (0-60)
    uint8_t minute = timeinfo.tm_min;       // Minutes after the hour (0-59)
    uint8_t hour = timeinfo.tm_hour;        // Hours since midnight (0-23)
    uint16_t day = timeinfo.tm_mday;        // Day of the month (1-31)
    uint8_t month = timeinfo.tm_mon + 1;    // Months since January (0-11, so add 1)
    uint16_t year = timeinfo.tm_year + 1900; // Years since 1900
    // Format the string
    sprintf(buffer, "%4u-%02d-%02d@%02d:%02d:%02d", year, month, day, hour, minute, second);
    return buffer; // Return the dynamically allocated string
};


/**
 * @brief   Get a short timestamp string of the current local time.
 *
 * This function writes a formatted timestamp string (YYYY-MM-DD@HH:MM:SS)
 * into the buffer provided by the caller.
 *
 * @param[out] shortTime_out   Pointer to the output buffer for the timestamp string.
 * @param[in]  SST_len_out     Length of the output buffer.
 *
 * @note  Receiving 'Target'-buffer must be large enough to hold the formatted string.
 * 
 * Usage:  = getShortTimesStamp(ts, sizeof(ts)) ;
 * 
 */
void getShortTimesStamp(char *shortTime_out, size_t SST_len_out)
{   struct tm timeinfo;             // Struct to hold the time information
    time_t now = time(NULL);        // Get the current time 
    localtime_r(&now, &timeinfo);   // Convert the time to local time
    //---------------------------------------------------------------------
    // Format ouput chars/strings of pointer of char provided by caller 
    //           Example :  "2025-05-16@10:15:05" = 19+1 chars
    //---------------------------------------------------------------------
    snprintf(shortTime_out, SST_len_out, "%4u-%02d-%02d@%02d:%02d:%02d",
        timeinfo.tm_year + 1900, // year    %4u   (given relative Years since 1900) 
        timeinfo.tm_mon + 1,     // month   %02d  (given relative Months since January (0-11, so add 1))
        timeinfo.tm_mday,        // day     %02d   
        timeinfo.tm_hour,        // hour    %02d
        timeinfo.tm_min,         // minute  %02d   
        timeinfo.tm_sec          // second  %02d
    );
};


/*############################################################################
  SyncNTP_and_set_LocalTZ()
      Synchronize the time with NTP server and set the local timezone
  used by: caller
#############################################################################*/
esp_err_t SyncNTP_and_set_LocalTZ()
{   esp_err_t err= ESP_OK;                  // Init: Set default error code
    //------------------------------------------------------------------------
    // Set Log-Level for this component
    //------------------------------------------------------------------------    
    esp_log_level_set(TAG, CONFIG_MY_SNTP_LOG_LEVEL);  // Log-Level for NTP and local TZ   
    ESP_LOGI(TAG, "::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::");
    ESP_LOGI(TAG, "--  Sync Time from NTP-Server and set the Local-TimeZone");
    /*--------------------------------
      1. Get the NTP-Time
    ---------------------------------*/
    ESP_LOGI(TAG, "--  Request the current time from: %s", CONFIG_MY_SNTP_TIME_SERVER);
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_MY_SNTP_TIME_SERVER);
    config.sync_cb = time_sync_notify_cb;     // Set callback (cb) funtion, when the time is synchronized is triggered
    err= esp_netif_sntp_init(&config);
    if (err == ESP_OK) {
      // Wait max 10s to get the NTP-Time
      int retry = 0; const int retry_count = 10;
      while (esp_netif_sntp_sync_wait(1000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
          ESP_LOGI(TAG, "--  ...waiting to receive response... (%d sec of max. %dsec)", retry, retry_count );  }
      // Check if reached the retry count
      if (retry == retry_count) {
          ESP_LOGE(TAG, "-- !! Failed to receive time from Server after %d attempts", retry_count); err = ESP_FAIL; // Set error code
      } else {
          ESP_LOGI(TAG, "--  * Time synchronized with NTP server: %s", CONFIG_MY_SNTP_TIME_SERVER);
      }
    } else {
          ESP_LOGE(TAG, "-- !! Failed Failed to initialize SNTP: %s", esp_err_to_name(err));
    }
    /*--------------------------------
      2. Set the local TimeZone
    ---------------------------------*/
    ESP_LOGI(TAG, "--  Set the local TimeZone: '%s'", CONFIG_MY_SNTP_TIME_ZONE);
    setenv("TZ", CONFIG_MY_SNTP_TIME_ZONE, 1); // Set localTimeZone 
    tzset(); // Set the TimeZone
    ESP_LOGI(TAG, "--  * Current local time is: %s", getShortTimestampString()); // Log the current time
    ESP_LOGI(TAG, "::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::");
    // END
    return err;
};
