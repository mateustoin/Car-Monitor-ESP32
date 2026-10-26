# Lógica pura de detecção de marcha: sem FreeRTOS nem hardware.
# API pública em include/; gear_learn.h, gear_detect.h e shift_advisor.h (priv_include/) são
# internos. Os testes do host os enxergam de propósito (caixa-branca): veja test/host/main.
idf_component_register(SRCS "gear_learn.c" "gear_detect.c" "shift_advisor.c" "gear_engine.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "priv_include"
                       REQUIRES vehicle_profile)
