# Interface: lógica pura (layout, cores, alertas) compilável em qualquer alvo, inclusive linux.
set(ui_srcs "ui_layout.c" "ui_colors.c" "ui_alert.c")
set(ui_requires gear_engine)
set(ui_priv_requires)

# Desenho LVGL (ui_screen.c, include/ui.h) só fora do alvo linux: os testes no host
# continuam sem LVGL. O lvgl vem público pelo board_api.
if(NOT ${IDF_TARGET} STREQUAL "linux")
    list(APPEND ui_srcs "ui_screen.c")
    list(APPEND ui_requires board_api)
    list(APPEND ui_priv_requires log)
endif()

idf_component_register(SRCS ${ui_srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES ${ui_requires}
                       PRIV_REQUIRES ${ui_priv_requires})
