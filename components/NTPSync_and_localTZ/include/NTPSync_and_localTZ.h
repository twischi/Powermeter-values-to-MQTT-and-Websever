/*----------
   INCLUDES
------------*/
#include <time.h>               // For time, ctime, localtime, strftime
#include <esp_log.h>            // For ESP-logging
#include <esp_err.h>            // For ESP-Error handling
/*------------
   STRUCTURES
--------------*/

/*------------------------
  Define PUBLIC FUNCTIONS
------------------------*/
esp_err_t SyncNTP_and_set_LocalTZ();

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
 */

char* getShortTimestampString();

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
 */
void getShortTimesStamp(char *shortTime_out, size_t SST_len_out);