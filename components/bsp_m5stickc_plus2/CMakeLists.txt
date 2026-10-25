# BSP do M5StickC Plus2. Fica vazio quando outra placa está escolhida em Kconfig.
# Na expansão antecipada (early expansion) o sdkconfig ainda não existe, então o
# teste só vale na fase de processamento; ali as REQUIRES já foram coletadas.
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT CONFIG_BOARD_M5STICKC_PLUS2)
    idf_component_register()
    return()
endif()

idf_component_register(SRCS "bsp.c"
                       PRIV_REQUIRES board_api esp_driver_gpio esp_driver_ledc esp_driver_spi
                                     esp_lcd esp_timer esp_hw_support log)
