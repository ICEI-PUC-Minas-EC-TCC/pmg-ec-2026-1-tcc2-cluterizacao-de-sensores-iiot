FILE(GLOB MAIN_FILES src/*/*.cpp)

set(EXTRA_REQUIRES "")
if(CONFIG_AMMETER_BACKEND_INA219)
    list(APPEND EXTRA_REQUIRES esp_driver_i2c)
endif()

idf_component_register(
    SRCS
        "src/main.cpp"
        ${MAIN_FILES}

    INCLUDE_DIRS
        "include"

    REQUIRES
        mqtt driver nvs_flash esp_wifi esp_adc esp_netif ${EXTRA_REQUIRES}
)
