file(GLOB_RECURSE srcs "main.c" "driver/*.c")

idf_component_register(SRCS "${srcs}"
                    PRIV_REQUIRES spi_flash esp_adc nvs_flash
                    INCLUDE_DIRS "")
