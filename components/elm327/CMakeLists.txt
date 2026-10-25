# Fontes puras: sem FreeRTOS. Compila igual no alvo linux e no ESP32.
idf_component_register(SRCS "elm327_parse.c" "elm327.c"
                       INCLUDE_DIRS "include"
                       REQUIRES obd_transport)
