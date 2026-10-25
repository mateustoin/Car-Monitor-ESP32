# Os dois BSPs entram sempre: as REQUIRES não podem depender do Kconfig (a expansão
# antecipada roda sem sdkconfig), então cada BSP fica vazio quando não é o escolhido.
idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES board_api bsp_m5stickc_plus2 bsp_headless ui gear_engine freertos log)
