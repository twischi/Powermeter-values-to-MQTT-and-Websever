idf_component_register(SRCS "xlan_connection.c"
                       PRIV_REQUIRES esp_eth esp_wifi nvs_flash lwip
                       INCLUDE_DIRS "include" )