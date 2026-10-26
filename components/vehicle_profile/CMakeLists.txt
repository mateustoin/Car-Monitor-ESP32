# Dados puros dos perfis de veículo: sem FreeRTOS nem hardware.
idf_component_register(SRCS "vehicle_profile.c"
                            "profiles/sandero_k7m.c"
                            "profiles/generic_manual.c"
                       INCLUDE_DIRS "include")
