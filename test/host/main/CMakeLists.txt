# Todo arquivo test_*.c entra automaticamente; novos componentes entram em REQUIRES
file(GLOB TEST_SOURCES "test_*.c")

idf_component_register(SRCS ${TEST_SOURCES}
                       REQUIRES unity obd_transport obd_recorder elm327 vehicle_profile gear_engine obd_poller storage nvs_flash ui power_mgr)

# Testes de caixa-branca dos headers internos do gear_engine (priv_include/). Declarar o
# diretório em PRIV_INCLUDE_DIRS daqui faz a validação de componentes da IDF avisar que ele
# pertence a outro componente; o include só no alvo de compilação deste componente de teste,
# com o caminho vindo da própria IDF (COMPONENT_DIR), deixa a exceção explícita e restrita aos
# testes, sem expor os headers internos a quem usa o gear_engine.
idf_component_get_property(gear_engine_dir gear_engine COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${gear_engine_dir}/priv_include")
