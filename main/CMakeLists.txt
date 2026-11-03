idf_component_register(
    SRCS "main.c" "placa.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_adc esp_timer esp_hw_support
)