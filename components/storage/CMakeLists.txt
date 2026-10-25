# Persistência NVS (blob de relações com versão e CRC32).
idf_component_register(SRCS "storage.c"
                       INCLUDE_DIRS "include"
                       REQUIRES vehicle_profile nvs_flash esp_rom)
