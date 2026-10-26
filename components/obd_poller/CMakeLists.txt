# Agenda de PIDs OBD pura: sem FreeRTOS nem transporte. Usa só as constantes ELM_PID_*.
idf_component_register(SRCS "obd_poller.c"
                       INCLUDE_DIRS "include"
                       REQUIRES elm327)
