/*===========================================================================================
 * @file        lan_connection.c
 * @author      Thomas Wisniewski
 * @date        2025-06-20
 * @brief       Connect to LAN with WiFi or Ethernet(Shield) including internet access 
 *
 * @note        This file is part of the ESP32 project.
 *              Based on Espressif example to use static ip: 
 *              https://github.com/espressif/esp-idf/tree/v5.4.1/examples/protocols/static_ip
 * 
 * @menuconfig  YES see below
 * 
 *  Connect to LAN
 *    This file contains the functions to connect to a LAN using Ethernet or WIFI.
 *    It uses the ESP-IDF framework and the ESP32 microcontroller.
 *    The functions are used to initialize the Ethernet driver, create a network interface,
 *    set a hostnane (optional), fixed IP (optinal) and handle events with LAN connection.
=============================================================================================*/
/*--------------------------------------------------------- 
   INCLUDES 
*---------------------------------------------------------*/
//#include <string.h>             // For memset(), strcpy(), strncmp()
// Core
#include "xlan_connection.h"    // OWN header file
#include "esp_log.h"            // For logging
#include "freertos/FreeRTOS.h"  // For FreeRTOS functions
#include "freertos/task.h"      // For FreeRTOS task functions
#include "freertos/event_groups.h" // For FreeRTOS event groups
#include "esp_event.h"          // For esp_event_xxx functions
// Network Interface
#include "esp_netif.h"          // For esp_netif_xxx functions
#include "lwip/ip4_addr.h"      // For ip4addr_aton() and ip4_addr_t
#include <netdb.h>              // For gethostbyname() and struct hostent
#include "ping/ping_sock.h"      // For ping functions
// Ethernet
#include "ethernet_init.h"      // For eth_init() and eth_deinit()
#include "esp_eth.h"            // For Ethenet functions (Shields, LAN8720, etc.)
// WiFi
#include "esp_wifi.h"           // For WiFi functions (if WiFi is used)
#include "nvs_flash.h"          // For NVS Flash functions (WiFi)
// mDNS
#include "mdns.h"               // For mDNS functions


/*--------------------------------------------------------- 
   Check if menuconfig (Ethernet or WiFI) is set valide
*---------------------------------------------------------*/
#if CONFIG_XLAN_USE_WIFI && CONFIG_XLAN_USE_ETH
#error "❌❌ ERROR - Invalid (menu)-config: Use Either select WiFi or Ethernet, NOT BOTH"
#endif
#if !(CONFIG_XLAN_USE_WIFI || CONFIG_XLAN_USE_ETH)
#error "❌❌ ERROR - Invalid (menu)-config: Incomplete network interface configuration, Either select WiFi or Ethernet."
#endif

/*------------------------------------------------------
   SETTINGS (Compiler runtime)
-------------------------------------------------------*/
// HINT: The following settings are set in the menuconfig
//       > Component config > Ethernet > Ethernet Hostname
//       > Component config > Ethernet > Use static IP address
//       > Component config > Ethernet > Static IP address
//       > Component config > Ethernet > Static Netmask
//       > Component config > Ethernet > Static Gateway
#ifdef CONFIG_XLAN_USE_WIFI
    #define XLAN_WIFI_SSID             CONFIG_XLAN_WIFI_SSID
    #define XLAN_WIFI_PASS             CONFIG_XLAN_WIFI_PASSWORD
    #define XLAN_MAXIMUM_RETRY         CONFIG_XLAN_MAXIMUM_RETRY
#endif // XLAN_USE_WIFI

#define XLAN_STATIC_IP_ADDR        CONFIG_XLAN_STATIC_IP_ADDR
#define XLAN_STATIC_NETMASK_ADDR   CONFIG_XLAN_STATIC_NETMASK_ADDR
#define XLAN_STATIC_GW_ADDR        CONFIG_XLAN_STATIC_GW_ADDR

#ifdef CONFIG_XLAN_STATIC_DNS_AUTO
    #define XLAN_MAIN_DNS_SERVER       XLAN_STATIC_GW_ADDR
    #define XLAN_BACKUP_DNS_SERVER     "0.0.0.0"
#else
    #define XLAN_MAIN_DNS_SERVER       CONFIG_XLAN_STATIC_DNS_SERVER_MAIN
    #define XLAN_BACKUP_DNS_SERVER     CONFIG_XLAN_STATIC_DNS_SERVER_BACKUP
#endif // CONFIG_XLAN_STATIC_DNS_AUTO

#ifdef CONFIG_XLAN_STATIC_DNS_RESOLVE_TEST
    #define XLAN_RESOLVE_DOMAIN        CONFIG_XLAN_STATIC_RESOLVE_DOMAIN
#endif // CONFIG_XLAN_STATIC_DNS_RESOLVE_TEST

#ifdef CONFIG_XLAN_USE_PING_GATEWAY
    #define XLAN_PING_GATEWAY_INTERVAL_SEC  CONFIG_XLAN_PING_GATEWAY_INTERVAL_SEC 
#endif // CONFIG_XLAN_USE_PING_GATEWAY

/*--------------------------------------------------------------
   The event group allows multiple bits for each event,
   but we only care about two events:
   - we are connected to the AP with an IP
   - we failed to connect after the maximum amount of retries 
----------------------------------------------------------------*/
#define CONNECTED_BIT           BIT0
#define WIFI_FAIL_BIT           BIT1
#define ETH_CONNECTION_TMO_MS   (10000) // Maximum 10s time to wait for ETHERNET connection in milliseconds
#define WIFI_CONNECTION_TMO_MS  (10000) // Maximum 10s time to wait for WIFI     connection in milliseconds

/*--------------------------------------------------------------
  VARIABLE & CONSTANTS
----------------------------------------------------------------*/
static const char *TAG   = "CON_xLAN";    // Use always 8 chars where possilbe >> TAG used for ESP-logging
#ifdef CONFIG_XLAN_USE_ETH
static const char *TETH  = "CON__ETH";    // Use always 8 chars where possilbe >> TAG used for ESP-logging
#else
static const char *TWIFI = "CON_WIFI";    // Use always 8 chars where possilbe >> TAG used for ESP-logging
#endif
static EventGroupHandle_t s_network_event_group; // FreeRTOS event group to signal when we are connected 
esp_netif_t *eth_netif;                          // Ethernet network interface pointer

bool lan_is_connected = false;          // Flag to check indicate if LAN is connected (INIT: false)
bool successful_ping_2gateway = true;   // Flag to check if ping to gateway was successful
bool ping_cycle_ended = true;           // Flag set to true when ping cycle is ended
char global_ip_info[16];                // Global variable to store IP information
char url_with_hostname[70];             // Global variable to store URL with hostname

/*#################################################################################################################################
   FUNCTIONS USED 4SETUP     FUNCTIONS USED 4SETUP     FUNCTIONS USED 4SETUP     FUNCTIONS USED 4SETUP.    FUNCTIONS USED 4SETUP   
##################################################################################################################################*/

/*--------------------------------------------------------------------------
 * @brief Set the DNS server for the given LAN interface (ETHERNET of WIFI)
 *
 * @return esp_err_t  
 *
 * used by: 'xlan_set_static_ip()'      
 *------------------------------------------------------------------------*/
static esp_err_t xlan_set_dns_server(esp_netif_t *netif, uint32_t addr, esp_netif_dns_type_t type)
{  if (addr && (addr != IPADDR_NONE)) {
        esp_netif_dns_info_t dns;
        dns.ip.u_addr.ip4.addr = addr;
        dns.ip.type = IPADDR_TYPE_V4;
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, type, &dns));
        if (type == ESP_NETIF_DNS_MAIN) {
             ESP_LOGI(TAG, "--            (a)  Set DNS server for MAIN to   : " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
        } else if (type == ESP_NETIF_DNS_BACKUP) {
             ESP_LOGI(TAG, "--            (b) Set DNS server for BACKUP to : " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
        }
    }
    return ESP_OK;
}
/*--------------------------------------------------------------------------------------------------
 * @brief  Set static IP address, netmask, and gateway for the given LAN interface (Ethernet or Wi-Fi).
 *
 * This function disables DHCP and assigns a static IP configuration including DNS servers to
 * the specified network interface. It is typically used to requiring a fixed IP instead of DHCP.
 *
 * @param[in]  netif  Pointer to the network interface (esp_netif_t*) to configure.
 *
 * @return esp_err_t (error status code)
 *         - ESP_OK on success
 *         - Appropriate esp_err_t on failure (logged internally)
 *
 * @note
 *   - This function is used by `eth_init()` or `wifi_event_handler()`.
 *   - Assumes `XLAN_STATIC_IP_ADDR`, `XLAN_STATIC_NETMASK_ADDR`, `XLAN_STATIC_GW_ADDR`,
 *     `XLAN_MAIN_DNS_SERVER`, and `XLAN_BACKUP_DNS_SERVER` are predefined.
 *
 * @see
 *   - esp_netif_dhcpc_stop()
 *   - esp_netif_set_ip_info()
 *   - esp_netif_set_dns_info()
 *-------------------------------------------------------------------------------------------------*/
static void xlan_set_static_ip(esp_netif_t *netif)
{   esp_err_t err= ESP_OK; // Init: Set default error code
    /*--------------------------------------------------
       Stop DHCP client for the Ethernet interface
    ---------------------------------------------------*/
    ESP_LOGI(TAG, "--  (+) Use STATIC IP for Ethenet-Connection' ");
    ESP_LOGI(TAG, "--      (I)   Stop DHCP                                >> 'esp_netif_dhcpc_stop'");
    err = esp_netif_dhcpc_stop(netif); // Stop DHCP client for the Ethernet interface
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not stop DHCP client for Ethernet interface! Error code: %s", esp_err_to_name(err));
        return; } // Return error code if stopping DHCP client fails
    /*--------------------------------------------------
       Fill a structure for static IP address
    ---------------------------------------------------*/
    esp_netif_ip_info_t ip;                             
        memset(&ip, 0 , sizeof(esp_netif_ip_info_t));           // Initialize memory for ip_info to zero
        ip.ip.addr = ipaddr_addr(XLAN_STATIC_IP_ADDR);          // Static IP
        ip.netmask.addr = ipaddr_addr(XLAN_STATIC_NETMASK_ADDR);// Static Netmask
        ip.gw.addr = ipaddr_addr(XLAN_STATIC_GW_ADDR);          // Static Gateway
    /*--------------------------------------------------
       Set your desired static IP, netmask, and gateway
    ---------------------------------------------------*/
    ESP_LOGI(TAG, "--      (II)  Set Static-IP                            >> 'esp_netif_set_ip_info'");
    err = esp_netif_set_ip_info(netif, &ip);                    // Set static
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not set static IP address to %s for Ethernet interface! Error code: %s", CONFIG_XLAN_STATIC_IP_ADDR, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "--            ✅ Static IP set to: %s", CONFIG_XLAN_STATIC_IP_ADDR);  }
    /*--------------------------------------------------
       Set IP for DNS server and set DNS server
    ---------------------------------------------------*/
    ESP_LOGI(TAG, "--      (III) Set DNS Server                           >> 'esp_netif_set_dns_info'");
    err = xlan_set_dns_server(netif, ipaddr_addr(XLAN_MAIN_DNS_SERVER), ESP_NETIF_DNS_MAIN); // Set main DNS server
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not set DNS server for Ethernet interface! Error code: %s", esp_err_to_name(err));}
    ESP_ERROR_CHECK(xlan_set_dns_server(netif, ipaddr_addr(XLAN_BACKUP_DNS_SERVER), ESP_NETIF_DNS_BACKUP));
}
/*---------------------------------------------------------------------
 * @brief wifi_event_handler() ) Event handler for WiFi Events
 *
 * @return esp_err_t
 *
 *  used by: 'esp_event_handler_instance_register'
 *--------------------------------------------------------------------*/
#ifdef CONFIG_XLAN_USE_WIFI
static void wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{   static int s_retry_num = 0; // Init: Retry counter for WiFi connection attempts
    if (event_base == WIFI_EVENT) 
    {   /*----------------------------------------------
          REACT on WIFI-Events
        -----------------------------------------------*/
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect(); // Start WiFi connection when station mode starts
                ESP_LOGD(TWIFI, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                ESP_LOGD(TWIFI, "!~~  WIFI-Event!: STARTED in Station mode, waiting for connection");
                ESP_LOGD(TWIFI, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");                
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TWIFI, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                ESP_LOGI(TWIFI, "!~~  WIFI-Event!: CONNECTED to Access Point (=LAN connected)");
                ESP_LOGI(TWIFI, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");   
#ifdef CONFIG_XLAN_USE_STATIC_IP_ADDR
                xlan_set_static_ip(arg); // Set static IP when connected to WiFi
#endif // CONFIG_XLAN_USE_STATIC_IP_ADDR
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TWIFI, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                ESP_LOGI(TWIFI, "!~~  WIFI-Event!: DISCONNECTED reconnect-try %d of %d",s_retry_num, XLAN_MAXIMUM_RETRY);
                lan_is_connected = false; // Set the flag to false for disconnection
                if (s_retry_num < XLAN_MAXIMUM_RETRY) {
                    esp_wifi_connect();
                    s_retry_num++;        
                } else {
                    xEventGroupSetBits(s_network_event_group, WIFI_FAIL_BIT);
                }
                ESP_LOGI(TWIFI, "!~~               ❌ Reconnect to failed");
                ESP_LOGI(TWIFI, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                break;
            default: // All other events ignored
                break;
        }
    }    
    if (event_base == IP_EVENT)
    {   /*----------------------------------------------
          REACT on IP Events
        -----------------------------------------------*/
         switch (event_id) {
            case IP_EVENT_STA_GOT_IP: // Ethernet got IP address
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data; // Transform event data to ip_event_got_ip_t type
                const esp_netif_ip_info_t *ip_info = &event->ip_info;       // Get IP information from event data
                ESP_LOGI(TWIFI, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                ESP_LOGI(TWIFI, "!~~  IP-Event!: WIFI GOT IP Address");
                ESP_LOGI(TWIFI, "!~~         IP: " IPSTR, IP2STR(&ip_info->ip));
                ESP_LOGI(TWIFI, "!~~       Mask: " IPSTR, IP2STR(&ip_info->netmask));
                ESP_LOGI(TWIFI, "!~~    GateWay: " IPSTR, IP2STR(&ip_info->gw));
                ESP_LOGI(TWIFI, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                s_retry_num = 0;
                snprintf(global_ip_info, sizeof(global_ip_info), IPSTR, IP2STR(&ip_info->ip)); // Convert IP address to string
                lan_is_connected = true; // Set the flag to true for successful connection
                xEventGroupSetBits(s_network_event_group, CONNECTED_BIT);
                break;
            default: // All other events ignored
                break;
        }
    }   
}
/*---------------------------------------------------------------------
 * wifi_init_sta()
 *--------------------------------------------------------------------*/
static esp_err_t wifi_init_sta(void)
{   esp_err_t err= ESP_OK;    // Init: Set default error code
    ESP_LOGI(TWIFI, "--  (4) Initialze None-Volatile-Flash (NVS) for WiFi   >> 'nvs_flash_init'");
    err = nvs_flash_init();   // Init:  NVS
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase()); // Erase NVS if no free pages or new version found
      err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TWIFI, "-- ❌ ERROR: Could not initialize NVS Flash for WiFi! Error code: %s", esp_err_to_name(err));
        return err; // Return error code if NVS initialization fails
    }   
    ESP_LOGI(TWIFI, "--  (5) Initialze WIFI in Station-Mode (= LAN Client)  >> 'esp_wifi_init'");    
    //------------------------------------
    // Settings for the WIFI interface
    //------------------------------------
    eth_netif = esp_netif_create_default_wifi_sta();
    assert(eth_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    //------------------------------------
    // INIT WIFI
    //------------------------------------
    err = esp_wifi_init(&cfg); // Initialize WiFi with default configuration
    if (err != ESP_OK) {
        ESP_LOGE(TWIFI, "-- ❌ ERROR: Could not initialize WiFi! Error code: %s", esp_err_to_name(err));
        return err; } // Return error code
    /*---------------------------------------------------
      REGISTER Event-Handlers for WIFI and IP events
    ----------------------------------------------------*/    
    esp_event_handler_instance_t instance_any_id; esp_event_handler_instance_t instance_got_ip;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, eth_netif, &instance_any_id);
    ESP_ERROR_CHECK(err); // Check if event handler registration was successful
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, eth_netif, &instance_got_ip);
    ESP_ERROR_CHECK(err); // Check if event handler registration was successful
    /*---------------------------------------------------------------- 
        Set authentication mode and password for the WiFi connection.
    ----------------------------------------------------------------*/    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = XLAN_WIFI_SSID,
            .password = XLAN_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. In case your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    err = esp_wifi_set_mode(WIFI_MODE_STA); // Set WiFi mode to station (client) mode
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TWIFI, "--  (6) Initialze WIFI as Statiion                     >> 'esp_wifi_set_config'");   
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config); // Set WiFi configuration for station mode
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TWIFI, "--  (7) Start WIFI                                     >> 'esp_wifi_start'");   
    err = esp_wifi_start(); // Start the WiFi driver
    ESP_ERROR_CHECK(err);

    //ESP_LOGI(TWIFI, "wifi_init_sta finished.");
    /*---------------------------------------------------------------------------------------------
      Waiting either WIIF connection established (CONNECTED_BIT) or connection failed 
      for the maximum re-tries (WIFI_FAIL_BIT). The bits are set by wifi_event_handler() (see above)
    -----------------------------------------------------------------------------------------------*/
    ESP_LOGI(TWIFI, "--  (8) Wait for the WIFI connection (max %d seconds).",WIFI_CONNECTION_TMO_MS / 1000);
    EventBits_t bits = xEventGroupWaitBits( s_network_event_group,
                                            CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(WIFI_CONNECTION_TMO_MS));
    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & CONNECTED_BIT) {
        ESP_LOGD(TWIFI, "connected to ap SSID:%s password:%s", XLAN_WIFI_SSID, XLAN_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TWIFI, "-- ❌ Failed to connect to WIFI, SSID:%s, password:%s", XLAN_WIFI_SSID, XLAN_WIFI_PASS);
    } else {
        ESP_LOGE(TWIFI, "UNEXPECTED EVENT");
    }
    /*----------------------------------------------------------------------------------
     UNREGISTER Event Handler, after job is done
    -----------------------------------------------------------------------------------
    ESP_LOGI(TWIFI, "--  (9) Unregister Event-Handlers for WIFI and IP events and delete Event-Group");
    err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
    ESP_ERROR_CHECK(err);
    err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    ESP_ERROR_CHECK(err);
    vEventGroupDelete(s_network_event_group);*/
    return ESP_OK; // Return success code
}
#endif // CONFIG_XLAN_USE_WIFI

#ifdef CONFIG_XLAN_USE_ETH
/*--------------------------------------------------------------------------
   eth_spi_show_config(): Logs the SPI configuration for the Ethernet module
   used by: 'eth_init'
 *------------------------------------------------------------------------*/
static void eth_spi_show_config()
{   ESP_LOGI(TETH, "==      -.......................");
    ESP_LOGI(TETH, "==      - SPI HOST  >Num:  %02d  (SPI Port ID, valid for current Chip='%s': 0...%d )", CONFIG_ETHERNET_SPI_HOST, CONFIG_IDF_TARGET, CONFIG_SOC_SPI_PERIPH_NUM-(1));
    ESP_LOGI(TETH, "==      - SPI Speed >MHZ:  %02d  (SPI Bus Clock Speed)",        CONFIG_ETHERNET_SPI_CLOCK_MHZ);
    ESP_LOGI(TETH, "==      -.......................");
    ESP_LOGI(TETH, "==      - SPI SCLK  GPIO:  %02d  (Bus Clock / speed)",          CONFIG_ETHERNET_SPI_SCLK_GPIO);
    ESP_LOGI(TETH, "==      - SPI MOSI  GPIO:  %02d  (Master OUT, Slave IN)",       CONFIG_ETHERNET_SPI_MOSI_GPIO);
    ESP_LOGI(TETH, "==      - SPI MISO  GPIO:  %02d  (Master IN, Slave OUT)",       CONFIG_ETHERNET_SPI_MISO_GPIO);
    ESP_LOGI(TETH, "==      - SPI CS   >GPIO:  %02d  (Chip Select)",                CONFIG_ETHERNET_SPI_CS0_GPIO);
    ESP_LOGI(TETH, "==      - INT      >GPIO:  %02d  (Interrupt-Signal from ETH Module to Master)", CONFIG_ETHERNET_SPI_INT0_GPIO);
    ESP_LOGI(TETH, "==      - RST      >GPIO:  %02d  (Reset forces ETH Module to restart)", CONFIG_ETHERNET_SPI_PHY_RST0_GPIO);
    ESP_LOGI(TETH, "==      -.......................");
    ESP_LOGI(TETH, "==      - SPI PHY  >ADDR:  %02d  (A Software Address)", CONFIG_ETHERNET_SPI_PHY_ADDR0);
}    
/*---------------------------------------------------------------------
 * eth_event_handler(): Event handler for ETHENET Events
   used by: 'esp_event_handler_instance_register'
 *--------------------------------------------------------------------*/
static void eth_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{   if (event_base == ETH_EVENT) 
    {   /*----------------------------------------------
          REACT on ETH-Events
        -----------------------------------------------*/
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED: // Ethernet link comming up
                ESP_LOGI(TETH, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                ESP_LOGI(TETH, "!~~  ETH-Event!: ETHERNET Link comes UP = CONNECTED");
                ESP_LOGI(TETH, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
#ifdef CONFIG_XLAN_USE_STATIC_IP_ADDR                
                xlan_set_static_ip(arg); // Set static IP address, netmask and gateway
#endif // CONFIG_XLAN_USE_STATIC_IP_ADDR
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGI(TETH, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                ESP_LOGI(TETH, "!~~  ETH-Event!: Ethernet Link went Down = DISCONNECTED");
                ESP_LOGI(TETH, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                lan_is_connected = false; // Set the flag to false for disconnection
                break;
            case ETHERNET_EVENT_START:
                ESP_LOGD(TETH, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                ESP_LOGD(TETH, "--  ETH-Event!: Ethernet Started");
                ESP_LOGD(TETH, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                break;
            case ETHERNET_EVENT_STOP:
                ESP_LOGD(TETH, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                ESP_LOGD(TETH, "--  ETH-Event!: Ethernet Stopped");
                ESP_LOGD(TETH, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                break;
            default: // All other events ignored
                break;
        }
    }
    if (event_base == IP_EVENT)
    {   /*----------------------------------------------
          REACT on IP Events
        -----------------------------------------------*/
        switch (event_id) {
            case IP_EVENT_ETH_GOT_IP: // Ethernet got IP address
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data; // Transform event data to ip_event_got_ip_t type
                const esp_netif_ip_info_t *ip_info = &event->ip_info;       // Get IP information from event data
                ESP_LOGI(TETH, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                ESP_LOGI(TETH, "!~~  IP-Event!: ETHERNET GOT IP Address");
                ESP_LOGI(TETH, "!~~         IP: " IPSTR, IP2STR(&ip_info->ip));
                ESP_LOGI(TETH, "!~~       Mask: " IPSTR, IP2STR(&ip_info->netmask));
                ESP_LOGI(TETH, "!~~    GateWay: " IPSTR, IP2STR(&ip_info->gw));
                ESP_LOGI(TETH, "!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!");
                snprintf(global_ip_info, sizeof(global_ip_info), IPSTR, IP2STR(&ip_info->ip)); // Convert IP address to string
                lan_is_connected = true; // Set the flag to true for successful connection
                xEventGroupSetBits(s_network_event_group, CONNECTED_BIT); // Set the CONNECTED_BIT in the event group to indicate successful connection
                break;
            default: // All other events ignored
                break;
        }
    }
}
/*--------------------------------------------------------------------------------------
 * @brief eth_init() = Initialize the Ethernet driver and attach it to the TCP/IP stack
 *
 * @return esp_err_t
 *
 *  used by: 'connect_to_xlan()'
 *------------------------------------------------------------------------------------*/
static esp_err_t eth_init(void)
{   esp_err_t err= ESP_OK;      // Init: Set default error code
    uint8_t eth_port_cnt = 0;   // Init: Ethernet port count to 0
    esp_eth_handle_t *eth_handles;
    ESP_LOGI(TETH, "--  (4) Settings for Physical-layer(PHY) of ETH-Module >> 'eth_init'");
    eth_spi_show_config(); // Show SPI configuration for the Ethernet module
    err= ethernet_init_all(&eth_handles, &eth_port_cnt); // Initialize all Ethernet drivers, could be more the one actived in menuconfig
    if (err != ESP_OK) {
        ESP_LOGE(TETH, "-- ❌ ERROR: Could not initialize  Physical-layer(PHY) of Ethernet (driver)! Error code: %s", esp_err_to_name(err)); return err; // Return error code if initialization fails
    } 
    if (eth_port_cnt > 1) {
        ESP_LOGW(TETH, "Multiple Ethernet devices detected, the first initialized is to be used!");
    }
    //------------------------------------
    // Settings for the Ethernet interface
    //------------------------------------
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH(); // Start with default Ethernet configuration
    eth_netif = esp_netif_new(&cfg);     // Create a new Ethernet interface
    //----------------------------------------
    // Attach Ethernet driver to TCP/IP stack
    //----------------------------------------
    ESP_LOGI(TETH, "--  (5) ATTACH Software TCP/IP-Layer with ETH-Hardware >> 'esp_netif_attach'");
    err = esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])); // Attach the Ethernet driver to the TCP/IP stack
    if (err != ESP_OK) {
        ESP_LOGE(TETH, "-- ❌ ERROR: Could not attach Software TCP/IP-Layer with ETH-Hardware! Error code: %s", esp_err_to_name(err)); return err; // Return error code if initialization fails
    }
    /*---------------------------------------------------
      REGISTER Event-Handlers for Ethernet and IP events
    ----------------------------------------------------*/
    esp_event_handler_instance_t instance_any_id;  esp_event_handler_instance_t instance_got_ip;
    ESP_LOGI(TETH, "--  (6) REGISTER Event-Handlers for                    >> 'esp_event_handler_instance_register'");
    ESP_LOGI(TETH, "--      (a) ETH-Events: ETH_EVENT                         handler= 'eth_event_handler, instance_any_id'");
    err= esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, eth_netif, &instance_any_id); 
    if (err != ESP_OK) {
        ESP_LOGE(TETH, "-- ❌ ERROR: Could not register ETH-Events handler! Error code: %s", esp_err_to_name(err)); return err; // Return error code if initialization fails
    }
    ESP_LOGI(TETH, "--      (b) IP-Events:  IP_EVENT                          handler= 'eth_event_handler, instance_got_ip'");
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler, eth_netif, &instance_got_ip); 
    if (err != ESP_OK) {
        ESP_LOGE(TETH, "-- ❌ ERROR: Could not register IP-Events handler! Error code: %s", esp_err_to_name(err)); return err; // Return error code if initialization fails
    }   
    /*---------------------------------------------------
      Start Ethernet 
    ----------------------------------------------------*/
    ESP_LOGI(TETH, "--  (7) START the Ethernet-Connection                  >> 'esp_eth_start' ");
    err = esp_eth_start(eth_handles[0]); // Start the Ethernet driver    
    if (err != ESP_OK) {
        ESP_LOGE(TETH, "-- ❌ ERROR: Could not start Ethernet driver state machine! Error code: %s", esp_err_to_name(err)); return err; // Return error code if initialization fails
    //} else {
    //    ESP_LOGI(TAG, "--      ✅ Ethernet/LAN started. Reachable (via hostname): %s", url_with_hostname);
    }
    /*----------------------------------------------------------------------------------
      Wait for lan to be established (CONNECTED_BIT) or timeout (ETH_CONNECTION_TMO_MS).      
      The bits are set by eth_event_handler() (see above) 
    ---------------------------------------------------------------------------------*/
    EventBits_t bits = xEventGroupWaitBits( s_network_event_group,
                                            CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(ETH_CONNECTION_TMO_MS));
    ESP_LOGI(TAG, "--  (8) Wait for the LAN connection (max %d seconds).",ETH_CONNECTION_TMO_MS / 1000);
    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (!(bits & CONNECTED_BIT)) {
        ESP_LOGE(TETH, "-- ❌ ERROR: Ethernet-link could not be established within timeout of %d msecs", ETH_CONNECTION_TMO_MS);
        err = ESP_FAIL; // Set error code if connection failed
    }
    /*----------------------------------------------------------------------------------
     UNREGISTER Event Handler, after job is done
    -----------------------------------------------------------------------------------
    ESP_LOGI(TETH, "--  (8) Unregister Event-Handlers for Ethernet and IP events and delete Event-Group");
    err = esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    ESP_ERROR_CHECK(err);
    err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, instance_got_ip);
    ESP_ERROR_CHECK(err);
    vEventGroupDelete(s_network_event_group);
    */
    return err; // Return error code if initialization fails
}
#endif // CONFIG_XLAN_USE_ETH

/*#################################################################################################################################
   PING GATEWAY    PING GATEWAY    PING GATEWAY    PING GATEWAY    PING GATEWAY    PING GATEWAY    PING GATEWAY    PING GATEWAY    
##################################################################################################################################*/
#ifdef CONFIG_XLAN_USE_PING_GATEWAY
/*---------------------------------------------------------------------
 * ping_success_event_handler(): Event handler for successful PING responses
   used by: ''
 *--------------------------------------------------------------------*/
static void ping_success_event_handler(esp_ping_handle_t hdl, void *args)
{   successful_ping_2gateway = true; // Set the flag to true for successful ping
/*    
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));                 // ->packet_hdr->seqno: {u16_t}     Sequence number of a ping procedure 
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));                       // ->packet_hdr->ttl:   {uint8_t}   Time to live of a ping procedure
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));    // ->recv_addr        : {ip_addr_t} IP address of replied target
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));            // ->recv_len         : {uint32_t}  Length of the received ICMP packet
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time)); // ->elapsed_time_ms  : {uint32_t}  Elapsed time between request and reply packet
    ESP_LOGD(TAG, "--  ✅ PING to Gateway successful: %s elapsed_time_ms= %" PRIu32, inet_ntoa(target_addr.u_addr.ip4), elapsed_time); // Log successful ping
    const char* str = (const char*) args; printf("%s : SUCCESS ", str); // "ping"
    printf("%" PRIu32 "bytes from %s icmp_seq=%d ttl=%d time= %" PRIu32 "ms\n", 
                    recv_len,                           // "%" PRIu32 
                    inet_ntoa(target_addr.u_addr.ip4),  // "%s" 
                    seqno,                              // "%d" 
                    ttl,                                // "%d" 
                    elapsed_time                        // "%" PRIu32 
    );
*/
}
/*---------------------------------------------------------------------
 * ping_timeout_event_handler(): Event handler for PING timeouts
   used by: ''
 *--------------------------------------------------------------------*/
static void ping_timeout_event_handler(esp_ping_handle_t hdl, void *args)
{   successful_ping_2gateway = false; // Set the flag to false for unsuccessful ping
/*  
    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));              // ->packet_hdr->seqno: {u16_t}      Sequence number of a ping procedure 
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr)); // ->recv_addr        : {ip_addr_t}  IP address of the target host 
    ESP_LOGE(TAG, "--  ❌ PING to Gateway failed: %s icmp_seq=%d timeout", inet_ntoa(target_addr.u_addr.ip4), seqno); // Log ping timeout
    const char* str = (const char*) args; printf("%s : TIMEOUT", str); // "ping"
    printf("From %s icmp_seq=%d timeout\n", inet_ntoa(target_addr.u_addr.ip4), seqno);  
*/
}
/*---------------------------------------------------------------------
 * ping_end_event_handler(): Event handler for the end of a PING session
   used by: ''
 *--------------------------------------------------------------------*/
static void ping_end_event_handler(esp_ping_handle_t hdl, void *args)
{   ping_cycle_ended = true; // Set the flag to true for end of ping cycle
/*     
    // Log end of ping session
    uint32_t transmitted; uint32_t received; uint32_t total_time_ms;
    const char* str = (const char*) args; printf("%s : END > ", str); // "ping"
    printf("%" PRIu32 " packets transmitted, %" PRIu32 " received, time %" PRIu32 " ms\n", transmitted, received, total_time_ms);
*/
}
#endif // CONFIG_XLAN_USE_PING_GATEWAY
/*---------------------------------------------------------------------
 * Task_ping_gateway(): Task to periodically ping the gateway 
   used by: ''
 *--------------------------------------------------------------------*/
static void Task_ping_gateway(void *pvParameter)
{   //==========================================================================
    // Initialize the PING session / Task
    //==========================================================================
    esp_err_t err= ESP_OK; // Init: Set default error code
    esp_ping_handle_t local_ping_handle = NULL;  // Use local handle
    //-----------------------------------------
     // Convert string IP for PING to ip_addr_t
    //-----------------------------------------
    ip_addr_t ping_id_addr; ipaddr_aton(XLAN_STATIC_GW_ADDR, &ping_id_addr); // Set the ip4 PING-target address to the static gateway address
    // -------------------------------------------------------------------------
    // Configure the PING data
    // -------------------------------------------------------------------------
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG(); // Initialize ping configuration with default values
    // Modify ping settings
    ping_config.count = 1;                  // Ping only once 
    ping_config.timeout_ms = 300;           // Timeout after 300 ms
    ping_config.target_addr = ping_id_addr; // Set the ip4 target address
    // -------------------------------------------------------------------------
    // set THREE callback functions for PING events
    // -------------------------------------------------------------------------
    esp_ping_callbacks_t cbs;
    cbs.on_ping_success = ping_success_event_handler;
    cbs.on_ping_timeout = ping_timeout_event_handler;
    cbs.on_ping_end = ping_end_event_handler;
    cbs.cb_args = "ping";  // arguments that feeds to all callback functions, can be NULL
     // -------------------------------------------------------------------------
    // Create a new ping session with the specified configuration and callbacks
    // -------------------------------------------------------------------------
    err = esp_ping_new_session(&ping_config, &cbs, &local_ping_handle); // Create a new ping session
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not create PING session! Error code: %s", esp_err_to_name(err));
        vTaskDelete(NULL);  // Critical: Delete task before return
        return; // Return error code if session creation fails
    } else {
        ESP_LOGI(TAG, "-- ✅ Activated frequent PING to Gateway = check working LAN connection : " IPSTR, IP2STR(&ping_id_addr.u_addr.ip4));
        // Store handle globally ONLY after successful creation
        // ping_handle = local_ping_handle;
    }
    while (1)
    {   //==========================================================================
        // Endless loop to ping the gateway every 5 minutes
        //==========================================================================
        ping_cycle_ended = false;          // Reset the flag to false before starting a new ping cycle
        err = esp_ping_start(local_ping_handle); // Start the ping session
        if (err != ESP_OK) {               // If starting the ping session fails
            ESP_LOGE(TAG, "-- ❌ ERROR: Could not start PING session! Error code: %s", esp_err_to_name(err));
            break; // Exit the loop if starting the ping session fails
        } 
        //---------------------------------------------------------------------------------
        // Wait max 1 sec for the ping session to finish, dedected by ping_cycle_ended flag
        //---------------------------------------------------------------------------------
        TickType_t xTicksToWait = pdMS_TO_TICKS(500); // Wait for max 500 ms
        TickType_t xStartTime = xTaskGetTickCount(); // Get the current tick
        while (xTaskGetTickCount() - xStartTime < xTicksToWait)
        {   if (ping_cycle_ended) { // Check if the ping cycle has ended
                break; // Exit the waiting loop
            } 
            vTaskDelay(pdMS_TO_TICKS(100)); // Delay for 100 ms to avoid busy waiting
        }
        //---------------------------------------------------------------------------------
        // Log the result of the ping session
        //---------------------------------------------------------------------------------        
        if (successful_ping_2gateway) { // Check if the ping was successful
            uint32_t total_time_ms;
            esp_ping_get_profile(local_ping_handle, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms)); // ->total_time_ms    : {uint32_t}   Total time of the ping procedure in milliseconds */
            ESP_LOGD(TAG, "--  ✅ PING to Gateway successful: resonse-time= %" PRIu32 "ms", total_time_ms);    // Log successful ping
        } else {                        // If the ping was not successful
            ip_addr_t target_addr;
            esp_ping_get_profile(local_ping_handle, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));      // ->recv_addr        : {ip_addr_t}  IP address of the target host 
            ESP_LOGE(TAG, "--  ❌ PING to Gateway failed: %s, timeout= %" PRIu32 "ms", inet_ntoa(target_addr.u_addr.ip4), ping_config.timeout_ms); // Log ping timeout
            break; // Exit the loop if the ping failed
        }
        //---------------------------------------------------------------------------------   
        // Wait to end the cycle
        //---------------------------------------------------------------------------------  
        vTaskDelay(pdMS_TO_TICKS( XLAN_PING_GATEWAY_INTERVAL_SEC * (1000) ));
    }
    // END THE PING TASK
    if (local_ping_handle) {
        esp_ping_stop(local_ping_handle);           // Stop the ping session 
        esp_ping_delete_session(local_ping_handle); // Delete the ping session
        local_ping_handle = NULL;                   // Set the local handle to NULL
        // ping_handle = NULL;                         // Set the global handle to NULL
    }
    ESP_LOGE(TAG, "--  ❌ STOPPED Task PING to Gateway ");
    vTaskDelete(NULL); // Delete this task
}

/*#################################################################################################################################
   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP   SETUP 
##################################################################################################################################*/
esp_err_t connect_to_xlan(void)
{   esp_err_t err= ESP_OK; // Init: Set default error code
    //------------------------------------------------------------------------
    // Set Log-Level for this component
    //      ESP_LOG_NONE <None>  -- ESP_LOG_ERROR <Errors> -- ESP_LOG_WARN <Warnings> 
    //      ESP_LOG_INFO <Info>  -- ESP_LOG_DEBUG <Debug>  -- ESP_LOG_VERBOSE <Verbose>  -- ESP_LOG_VERBOSE
    //------------------------------------------------------------------------
    esp_log_level_set(TAG, CONFIG_XLAN_LOG_LEVEL);  // Log-Level of this component see menuconfig
    //------------------------------------------------------------------------
    // Initialize global variables with blanks in case of get ip address fails 
    //------------------------------------------------------------------------
    memset(global_ip_info, ' ', 15);   global_ip_info[15] = '\0';
    ESP_LOGI(TAG, "------------------------------------------------------------------------------------");
#ifdef CONFIG_XLAN_USE_WIFI
    ESP_LOGI(TAG, "--  Connect to LAN via WIFI.");
    /*----------------------------------------------------------
       Switch off detailed Loggings for WIFI-Component
    ------------------------------------------------------------*/
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("wifi_init", ESP_LOG_NONE);
    esp_log_level_set("pp", ESP_LOG_NONE);
    esp_log_level_set("net80211", ESP_LOG_NONE);
    esp_log_level_set("phy_init", ESP_LOG_NONE);
#endif
#ifdef CONFIG_XLAN_USE_ETH
    ESP_LOGI(TAG, "--  Connect to LAN via ETHERNET.");
    /*----------------------------------------------------------
       Switch off detailed Loggings for ETHERNET
    ------------------------------------------------------------*/
    esp_log_level_set("esp_eth.netif.netif_glue", ESP_LOG_NONE);
    esp_log_level_set("ethernet_init",            ESP_LOG_NONE);
#endif
    ESP_LOGI(TAG, "------------------------------------------------------------------------------------");
    /*----------------------------------------------------------
       Switch off Loggings for components where I don't need them
    ------------------------------------------------------------*/
    esp_log_level_set("esp_netif_handlers", ESP_LOG_NONE);
    esp_log_level_set("mdns_mem"          , ESP_LOG_NONE);
    /*-----------------------------------------------------
      Create Event Group running in background
    ------------------------------------------------------*/
    ESP_LOGI(TAG, "--  (1) Create Event Group for Network-Events with     >> 'xEventGroupCreate'"); 
    s_network_event_group = xEventGroupCreate();        // "event_groups.c" @ .../esp-idf/components/freertos/include/freertos/event_groups.h
    /*-----------------------------------------------------
        Initialize TCP/IP stack 
    ------------------------------------------------------*/
    ESP_LOGI(TAG, "--  (2) Iniitzialize underlying TCP/IP stack with      >> 'esp_netif_init'");
    err = esp_netif_init();                             // "esp_netif_lwip.c" @ .../esp-idf/components/esp_netif/lwip/
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not initialize TCP/IP Interf.-Abstraction-Layer! Error code: %s", esp_err_to_name(err)); return err; // Return error code if initialization fails
    }     
    ESP_LOGI(TAG, "--  (3) Create default event loop for Network-Events   >> 'esp_event_loop_create_default'");
    /*-----------------------------------------------------
      Create default event loop that running in background
    ------------------------------------------------------*/
    err = esp_event_loop_create_default();              // "esp_event.c"       @ .../esp-idf/components/esp_event/
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not create default event loop for Network-Events! Error code: %s", esp_err_to_name(err)); return err; // Return error code if initialization fails
    }
    /*-----------------------------------------------------
      CONNECT to WIFI
    ------------------------------------------------------*/
#ifdef CONFIG_XLAN_USE_WIFI
    err = wifi_init_sta();
#endif
    /*-----------------------------------------------------
      CONNECT to ETHERNET
    ------------------------------------------------------*/ 
#ifdef CONFIG_XLAN_USE_ETH
    /*---------------------------------------
       Initialize Ethernet driver / Hardware
    ----------------------------------------*/
    err = eth_init(); // Initialize the Ethernet driver, ESP-IDF fct/lib: 'ethernet_init.h' @ .../esp-idf/components/ethernet/ 
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not initialize  Physical-layer(PHY) of Ethernet (driver)! Error code: %s", esp_err_to_name(err)); // return err; // Return error code if initialization fails
    }
#endif
    /*-----------------------------------------------------
      CHECK if a URL can be resolved to an IP address
    ------------------------------------------------------*/ 
#ifdef CONFIG_XLAN_STATIC_DNS_RESOLVE_TEST
    // ---------------------------------------------------------------------------------
    // Resolve the domain name to an IP address using getaddrinfo()
    // This is useful to check if the DNS resolution works correctly.
    // ---------------------------------------------------------------------------------
    if (lan_is_connected) { // ONLY If the LAN is connected
        struct addrinfo *address_info; struct addrinfo hints; // Declare variables for address information and hints
        memset(&hints, 0, sizeof(hints));                     // Initialize memonry for hints to zero
        hints.ai_family = AF_UNSPEC;                          // Allow both IPv4 and IPv6 addresses
        hints.ai_socktype = SOCK_STREAM;                      // Use stream sockets (TCP)
        ESP_LOGI(TAG, "--  (x) Trying to resolve IP address for domain: '%s'", XLAN_RESOLVE_DOMAIN);
        int res = getaddrinfo(XLAN_RESOLVE_DOMAIN, NULL, &hints, &address_info);
        if (res != 0 || address_info == NULL) {
                ESP_LOGE(TAG, "--      ❌ couldn't get hostname for :%s: "
                        "getaddrinfo() returns %d, addrinfo=%p", XLAN_RESOLVE_DOMAIN, res, address_info);
        } else {
            if (address_info->ai_family == AF_INET) {
                struct sockaddr_in *p = (struct sockaddr_in *)address_info->ai_addr;
                ESP_LOGI(TAG, "--      ✔︎ Resolved IPv4 address: %s", ipaddr_ntoa((const ip_addr_t*)&p->sin_addr.s_addr));
            }
  #if CONFIG_LWIP_IPV6
            else if (address_info->ai_family == AF_INET6) {
                struct sockaddr_in6 *p = (struct sockaddr_in6 *)address_info->ai_addr;
                ESP_LOGI(TAG, "--      ✔︎ Resolved IPv6 address: %s", ip6addr_ntoa((const ip6_addr_t*)&p->sin6_addr));
            }
  #endif
        } // End of if (res != 0 || address_info == NULL)
    }
#endif
#ifdef CONFIG_XLAN_USE_OWN_HOSTNAME
    /*---------------------------------------------
       Set a hostname for the Ethernet interface
    ----------------------------------------------*/
    ESP_LOGI(TAG, "--  (+) SET Ethernet-Hostname                          >> 'esp_netif_set_hostname' ");
    ESP_LOGI(TAG, "--      to: '%s'", CONFIG_XLAN_HOSTNAME);
    err= esp_netif_set_hostname(eth_netif, CONFIG_XLAN_HOSTNAME); // Init: Set default error code
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not set hostname for Ethernet interface! Error code: %s", esp_err_to_name(err));
    }
#endif
#ifdef CONFIG_XLAN_USE_STATIC_IP_ADDR
    // ---------------------------------------------------------------------------------
    // As with fixed IP, hostname above will be useable use mDNS as alternative to DNS
    // ---------------------------------------------------------------------------------
    err = mdns_init(); //initialize mDNS service
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not initialize mDNS service! Error code: %s", esp_err_to_name(err));
    }
    err = mdns_hostname_set(CONFIG_XLAN_HOSTNAME); //set hostname
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not set mDNS hostname! Error code: %s", esp_err_to_name(err));
    }
    err = mdns_instance_name_set("Hostname using mDNS"); //set default instance
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not set mDNS instance name! Error code: %s", esp_err_to_name(err));
    }
    // Add mDNS service for HTTP
    err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0); //add services
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "-- ❌ ERROR: Could not add mDNS service! Error code: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "--      (IV)  mDNS service for HTTP added successfully");
    }
    snprintf(url_with_hostname, sizeof(url_with_hostname), "http://%s.local", CONFIG_XLAN_HOSTNAME);
#else
    snprintf(url_with_hostname, sizeof(url_with_hostname), "http://%s",       CONFIG_XLAN_HOSTNAME);    
#endif // CONFIG_XLAN_USE_STATIC_IP_ADDR

#ifdef CONFIG_XLAN_USE_PING_GATEWAY
    if (lan_is_connected) { // ONLY If the LAN is connected
        esp_log_level_set("ping_sock"          , ESP_LOG_NONE); // Swith off detailed Loggings for ping_sock
        // ---------------------------------------------------------------------------------
        // Create a task to ping the gateway frequently
        // ---------------------------------------------------------------------------------
        xTaskCreate(Task_ping_gateway, "ping_gw_task", 4096 /* 4kb */, NULL, 6, NULL);
    }
#endif // CONFIG_XLAN_USE_PING_GATEWAY
    /*---------------------------------------
       Closing the initialization
    ----------------------------------------*/  
    ESP_LOGI(TAG, "------------------------------------------------------------------------------------");  
    return err; // Return error code if initialization fails
}

/********************************************************************************
   GETTERS   GETTERS   GETTERS   GETTERS   GETTERS   GETTERS   GETTERS   GETTERS   
*********************************************************************************/

// Getter-Function to get the IP information
char *get_lan_ip_info(void) {
    return global_ip_info; // Send back the global IP information
}

/*================================================================================
  is_lan_connected(): Give back the status of the LAN connection
    * This function checks if the LAN is connected and returns true or false.
    * It is used to determine if the device is connected to the LAN. 
  used by: caller
=================================================================================*/
bool is_lan_connected(void) {
    // Check if an OTA update is currently in progress
    return lan_is_connected;
}   

// Getter für das Flag
bool was_last_ping_successful(void) {
    return successful_ping_2gateway;
}
