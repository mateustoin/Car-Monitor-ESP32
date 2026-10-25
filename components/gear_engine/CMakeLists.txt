# Lógica pura de detecção de marcha: sem FreeRTOS nem hardware.
# API pública em include/; gear_learn.h, gear_detect.h e shift_advisor.h são internos.
idf_component_register(SRCS "gear_learn.c" "gear_detect.c" "shift_advisor.c" "gear_engine.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES vehicle_profile)
