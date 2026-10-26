# BSP sem tela nem buzzer (kits genéricos, p.ex. esp32c3). Fica vazio quando outra
# placa está escolhida. Na expansão antecipada o sdkconfig ainda não existe.
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT CONFIG_BOARD_HEADLESS)
    idf_component_register()
    return()
endif()

idf_component_register(SRCS "bsp.c"
                       PRIV_REQUIRES board_api esp_hw_support log)
