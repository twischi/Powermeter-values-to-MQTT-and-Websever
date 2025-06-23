/*
  To be FILLED
 */
#pragma once

/*----------
   INCLUDES
-----------*/
#include "esp_event.h"
#include "esp_netif.h"          // For TCPI function/conenections
#include "esp_eth.h"            // For ETHERNET connection

extern bool my_lan_Isconnected;     // Flag to check if my lan is connected
extern char global_ip_info[16];     // Global variable to store IP information
extern char url_with_hostname[70]; // Global variable to store URL with hostname

/*####################################
  Function Prototypes
#####################################*/
/**
 * @brief Initialize and connect to LAN.
 */
esp_err_t connect_to_xlan(void);

char *get_lan_ip_info(void); 

bool is_lan_connected(void);