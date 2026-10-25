# Fontes puras: compila igual no alvo linux (testes no host) e no ESP32.
# transport_bt_spp.c entra na Tarefa 13.
idf_component_register(SRCS "obd_transport.c" "transport_replay.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common obd_recorder)
