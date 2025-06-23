/*----------
   INCLUDES
------------*/
#include "esp_err.h"                // For ESP error codes
#include "mqtt_client.h"            // For MQTT client

/*------------
   STRUCTURES
--------------*/

/*------------------------
  Define PUBLIC FUNCTIONS
------------------------*/
esp_err_t Start_myMQTT_Client(esp_mqtt_client_handle_t *mqtt_client_handle_out, const char *timestamp_txt);

bool is_mqtt_connected(void);