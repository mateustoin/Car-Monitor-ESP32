# obd_transport.c e transport_replay.c são puros: compilam igual no alvo linux
# (testes no host) e no ESP32. transport_bt_spp.c só entra com o SPP habilitado.
set(srcs "obd_transport.c" "transport_replay.c" "transport_default.c")
if(CONFIG_OBD_TRANSPORT_BT_SPP)
    list(APPEND srcs "transport_bt_spp.c")
endif()

# As REQUIRES não podem depender do Kconfig (a expansão antecipada roda sem
# sdkconfig), então a dependência de bt segue o alvo: nunca no linux. No esp32c3
# o componente bt entra no grafo mas não compila nada (BT desligado).
set(priv_requires "")
idf_build_get_property(target IDF_TARGET)
if(NOT target STREQUAL "linux")
    list(APPEND priv_requires bt freertos log)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common obd_recorder
                       PRIV_REQUIRES ${priv_requires})
