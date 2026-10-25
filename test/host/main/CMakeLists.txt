# Todo arquivo test_*.c entra automaticamente; novos componentes entram em REQUIRES
file(GLOB TEST_SOURCES "test_*.c")

idf_component_register(SRCS ${TEST_SOURCES}
                       REQUIRES unity)
