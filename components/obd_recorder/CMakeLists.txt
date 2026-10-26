# rec_format.c é puro e compila em todos os alvos (inclusive linux, nos testes do host).
# obd_recorder.c (sinks serial/LittleFS e comandos de console) só entra fora do linux.
if(IDF_TARGET STREQUAL "linux")
    idf_component_register(SRCS "rec_format.c"
                           INCLUDE_DIRS "include"
                           REQUIRES esp_common)
else()
    idf_component_register(SRCS "rec_format.c" "obd_recorder.c"
                           INCLUDE_DIRS "include"
                           REQUIRES esp_common
                           PRIV_REQUIRES console joltwallet__littlefs freertos log)
endif()
