# API de placa: só o header. A implementação vem do BSP escolhido em Kconfig
# (bsp_m5stickc_plus2 ou bsp_headless); o lvgl entra pelo idf_component.yml.
idf_component_register(INCLUDE_DIRS "include"
                       REQUIRES esp_common)
