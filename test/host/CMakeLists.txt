cmake_minimum_required(VERSION 3.16)

# Componentes puros do projeto (compilados no host, sem hardware)
set(EXTRA_COMPONENT_DIRS ../../components)
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(host_test)
