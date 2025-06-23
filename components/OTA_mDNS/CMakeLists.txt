idf_component_register( SRCS "OTA_mDNS.c"
                        PRIV_REQUIRES app_update esp_https_ota esp_http_client
                        INCLUDE_DIRS "include")
