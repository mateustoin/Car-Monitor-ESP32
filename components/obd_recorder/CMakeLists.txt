# Parte pura (formato de linha). A gravação em SD/serial entra em tarefa posterior.
idf_component_register(SRCS "rec_format.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common)
