# Lógica pura de detecção de marcha: sem FreeRTOS nem hardware.
# "." expõe gear_learn.h (interno); a Tarefa 7 adiciona include/gear_engine.h.
idf_component_register(SRCS "gear_learn.c" "gear_detect.c"
                       INCLUDE_DIRS "."
                       REQUIRES vehicle_profile)
