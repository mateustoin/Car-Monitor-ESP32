# Interface: lógica pura (layout, cores, alertas) compilável em qualquer alvo, inclusive linux.
set(ui_srcs "ui_layout.c" "ui_colors.c" "ui_alert.c")
set(ui_requires gear_engine)

# Tarefa 12: o desenho LVGL entra aqui, fora do alvo linux, p.ex.
# if(NOT ${IDF_TARGET} STREQUAL "linux")
#     list(APPEND ui_srcs "ui_screen.c")
#     list(APPEND ui_requires lvgl)
# endif()

idf_component_register(SRCS ${ui_srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES ${ui_requires})
