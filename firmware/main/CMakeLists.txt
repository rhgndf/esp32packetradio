idf_component_register(SRCS "radio.cpp" "logging.cpp" "dhcp_server.c" "usb_descriptors.c" "usb_network.c" "wifi.cpp" "http_server.cpp" "main.cpp" 
                    INCLUDE_DIRS ".")
littlefs_create_partition_image(factory_data ../data FLASH_IN_PROJECT)