# Todo arquivo test_*.c entra automaticamente; novos componentes entram em REQUIRES
file(GLOB TEST_SOURCES "test_*.c")

# Os testes de unidade também enxergam os headers internos do gear_engine.
idf_component_register(SRCS ${TEST_SOURCES}
                       PRIV_INCLUDE_DIRS ../../../components/gear_engine
                       REQUIRES unity obd_transport obd_recorder elm327 vehicle_profile gear_engine obd_poller storage nvs_flash ui)
