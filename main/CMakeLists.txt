FILE(GLOB MAIN_FILES src/*/*.cpp)

idf_component_register(
    SRCS
        "src/main.cpp"
        ${MAIN_FILES}

    INCLUDE_DIRS
        "include"
        
    REQUIRES 
        mqtt driver nvs_flash esp_wifi esp_adc
)