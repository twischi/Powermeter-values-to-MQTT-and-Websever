/*===========================================================================================
 * @file        myMQTT.c
 * @author      Thomas Wisniewski
 * @date        2025-05-10
 * @brief       Component to esstablish a Client to my Home MQTT broker
 *
 * @menuconfig  YES
 * (includes)   YES
 * 
 * Startpoint was example from Espressif: 
 *    https://github.com/espressif/esp-idf/tree/v5.4.1/examples/protocols/mqtt/tcp
 *
 * See also API documentation:
 *   https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32/api-reference/protocols/mqtt.html#api-reference
 * 
 * How does this file work?
 *    ...
 * 
========================================================================================================*/
/*----------
   INCLUDES
-----------*/
#include "myMQTT.h"                 // For THIS component
/*------------------
  ESP Logging: TAG 
------------------*/
#include <stdio.h>                   // For printf
#include <esp_log.h>                // For ESP-logging
static const char *TAG = "MY_MQTT_"; // TAG for logging
/*                        12345678 */
/*----------------------------
   VARIABLES: Whole Component 
------------------------------*/
const char *mqtt_start_timestamp_txt; // Variable to transfer the 'timestamp_txt' Parameter to a global variable of this component
bool mqtt_is_connected = false;       // Flag to with info if MQTT is connected (INIT: false)
/*----------------------------
   Constants via #define 
------------------------------*/

/*###############################################################################
   log_error_if_nonzero()
    Helper function to log errors, checks if the error code is non-zero 
                                   and logs the error message.
  used by: mqtt_event_handler
################################################################################*/ 
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*###############################################################################
   mqtt_event_handler(): Event handler for MQTT events
    This function is called by the MQTT client event loop to handle various events.
   used by: Start_myMQTT_Workers
################################################################################*/ 
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{   /*  See here what different events can occur:
    *   -----------------------------------------
    *   https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32/api-reference/protocols/mqtt.html#events
    */
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    //~~~~~~~~~~~~~~~~~~~~~~~ 
    // EVENT: Got CONNECTION
    //~~~~~~~~~~~~~~~~~~~~~~~  
    case MQTT_EVENT_CONNECTED:  // The client has successfully established a connection to the broker.
        mqtt_is_connected = true; // Set the flag to true for successful connection
        ESP_LOGI(TAG, "--  ✅ MQTT_EVENT_CONNECTED");
        //........................................................................
        // Set the messsage for last successful connection with
        // given timestamp_txt, when caling 'Start_myMQTT_Workers' to the broker
        //........................................................................
        // Build the message to be sent
        char msg_payload[256];
        strcpy(msg_payload, "{\"Connection\":\"Sucessfull connected to MQTT-Broker.\",\"comment\":\"Status\",\"lastUpdate\":\""); // JSON-Payload
        strcat(msg_payload, mqtt_start_timestamp_txt);                                                                            // Add the timestamp 
        strcat(msg_payload, "\"}");                                                                                               // Add the closing bracket
        // Publish
        msg_id = esp_mqtt_client_publish(client,
            CONFIG_MY_MQTT_ROOT_TOPIC "/TXT/Status", // Set the topic to publish 
            msg_payload , 0,                // use this Payload
            CONFIG_MY_MQTT_QOS_DEFAULT,     // QoS level
            CONFIG_MY_MQTT_RETAIN_DEFAULT); // Retain flag 
        ESP_LOGD(TAG, "--   Publish successful, Start-Message with msg_id=%d", msg_id);
        break;
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    // EVENT: DIS-CONNECTION
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    case MQTT_EVENT_DISCONNECTED: // The client has aborted the connection due to being unable to read or write data, e.g., because the server is unavailable.
        ESP_LOGW(TAG, "--  🚨 MQTT_EVENT_DISCONNECTED");
        mqtt_is_connected = false; // Set the flag to false
        break;
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  
    // EVENT: SUBSCRIPTION SUCCESS
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    case MQTT_EVENT_SUBSCRIBED:  // The client has successfully subscribed to a topic
        ESP_LOGI(TAG, "--  MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  
    // EVENT: UN-SUBSCRIPTION SUCCESS
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~     
    case MQTT_EVENT_UNSUBSCRIBED: // The broker has acknowledged the client's unsubscribe request
        ESP_LOGI(TAG, "--  MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  
    // EVENT: PUBLISHED SUCCESS
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    case MQTT_EVENT_PUBLISHED: //The broker has acknowledged the client's publish message
        ESP_LOGD(TAG, "--  MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    // EVENT: RECEIVED DATA of an Subscription
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    case MQTT_EVENT_DATA: // The client has received a publish message
        ESP_LOGD(TAG, "Got: TOPIC=%.*s : DATA=%.*s", event->topic_len, event->topic, event->data_len, event->data);
        break;
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    // EVENT: ERROR happend
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  
    case MQTT_EVENT_ERROR: // The client has encountered an error
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGW(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    // EVENT: UN-KNOWN EVENT / OTHER event appeared 
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    default:
        ESP_LOGD(TAG, "--  ⚠️ OTHER EVENT, event id:%d", event->event_id);
        break;
    }
}

/*------------------------------------------------------------
  Public FUNCTION of this component: Start_myMQTT_Workers
-------------------------------------------------------------*/
esp_err_t Start_myMQTT_Client(esp_mqtt_client_handle_t *mb_handle_out, const char *timestamp_txt)
{   esp_err_t err= ESP_OK;                  // Init: Set default error code
    esp_mqtt_client_handle_t client = NULL; // Init: Handle to MQTT client
    /*------------------------------------------------------------------------
      Set Log-Levels for this component
    ------------------------------------------------------------------------*/    
    esp_log_level_set(TAG, CONFIG_MY_MQTT_LOG_LEVEL);  //  MY_MQTT_:  Log-Level for MQTT connection see menuconfig
    //esp_log_level_set("uart",           ESP_LOG_WARN); // JUST A TEMPLATE 
    /*------------------------------------------------
      0. Show Intro Message of this component
    -------------------------------------------------*/
    ESP_LOGD(TAG, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
    ESP_LOGD(TAG, "--  Start MQTT, myComponent: 'myMQTT'");
    ESP_LOGD(TAG, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
    /*--------------------------------------------------
      1. Check if given parameters are OK and hand over
    *--------------------------------------------------*/
    mqtt_start_timestamp_txt = timestamp_txt; // Set the global variable to the given parameter: This is used to send the timpestamp of the last successful connection message to the broker
    /*------------------------------------
      2. Initialize and Start MQTT Client.
    *-------------------------------------
      (a) esp_mqtt_client_init
      (b) esp_mqtt_client_register_event
      (c) esp_mqtt_client_start
    *------------------------------------*/
    // Fill the configuration structure 
    esp_mqtt_client_config_t mqtt_cfg = {
          // Set the MQTT broker URL  
          .broker.address.uri = CONFIG_MY_MQTT_BROKER_URL,
          // LAST WILL MESSAGE    
          .session =  { .last_will.topic = CONFIG_MY_MQTT_ROOT_TOPIC "/LW", // Topic for the last will message
                        .last_will.msg  = "{\"lastWill\":\"Lost connection: Try to restart the ESP\"}", // {"lastWill":"Lost connection: Try to restart the ESP"}
                        .last_will.qos = 1,                       // QoS level for the last will message
                        .last_will.retain = 1,                    // Retain flag for the last will message
                        .keepalive = CONFIG_MY_MQTT_KEEP_ALIVE }  // Keepalive interval in seconds, Default is 2min (120s)
    };
    // Execute INIT of the MQTT client
                              ESP_LOGD(TAG, "--  Init the MQTT-Client.");
    client = esp_mqtt_client_init(&mqtt_cfg);
    // Register the event handler for ALL MQTT events
                              ESP_LOGD(TAG, "--  Register the event handler for ALL MQTT events the client handles.");
    err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
                              ESP_LOGE(TAG, "--  ❌ ERROR: Could not register MQTT event handler! Error code: %s", esp_err_to_name(err));
    }
                              ESP_LOGD(TAG, "--  Start the MQTT-Client.");
    err = esp_mqtt_client_start(client);
    if (err != ESP_OK) {
                              ESP_LOGE(TAG, "--  ❌ ERROR: Could not start MQTT client! Error code: %s", esp_err_to_name(err));
    }
    /*---------------------------------------
      3. Wait for the client to be connected
    *----------------------------------------*/
                              ESP_LOGD(TAG, "--  Wait for the client to be connected (max 4 seconds).");
    int mqtt_conn_timeout = 4000; // Maximum wait time in milliseconds 
    while  (!mqtt_is_connected && ( mqtt_conn_timeout > 100))  {
      vTaskDelay(pdMS_TO_TICKS(100));  // Delay for the check interval of 100ms
      mqtt_conn_timeout -= 100; // Decrease the remaining wait time
    }
    if (mqtt_is_connected) {  ESP_LOGD(TAG, "--  * MQTT client successfully connected!");} 
    else {                    ESP_LOGW(TAG, "--  * MQTT client connection timed out after 4 seconds.");}  
    /*---------------------------------------
      X. Example: How to publish a message 
    *----------------------------------------*/
#ifdef CONFIG_MY_MQTT_SEND_TEST_MESSAGE  
    msg_id = esp_mqtt_client_publish(client, CONFIG_MY_MQTT_ROOT_TOPIC "/TXT/ExampleMsg", "Here can by your real Message.", 0,
                                             CONFIG_MY_MQTT_QOS_DEFAULT,     // QoS level
                                             CONFIG_MY_MQTT_RETAIN_DEFAULT); // Retain flag  
                              ESP_LOGI(TAG, "--  Published a Test-Message successful, msg_id=%d", msg_id);
#endif
    /*---------------------------------------
      X. Example: How to subscribe to a topic
    *----------------------------------------*/
#ifdef CONFIG_MY_MQTT_SUBSCRIBE_TESTING
    msg_id = esp_mqtt_client_subscribe(client, CONFIG_MY_MQTT_TOPIC_TO_SUBSCRIBE, 1);
    ESP_LOGI(TAG, "--  Subscribe to the topic '"CONFIG_MY_MQTT_TOPIC_TO_SUBSCRIBE"' with QoS 1, msg_id=%d", msg_id);
#endif
    /*-----------------------------------
      4. Hand over Handle to MQTT client
    *-----------------------------------*/  
    *mb_handle_out = client; // Hand over the handle to the MQTT client
    ESP_LOGD(TAG, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
    // END
    return err;
};

/*================================================================================
  is_mqtt_connected(): 
    * This function checks if the MQTT client is currently connected to the broker.
    * It returns true if connected, false otherwise. 
  used by: caller
=================================================================================*/
bool is_mqtt_connected(void) {
    // Check if an OTA update is currently in progress
    return mqtt_is_connected;
} 