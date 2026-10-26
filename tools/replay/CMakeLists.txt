cmake_minimum_required(VERSION 3.16)

# Replay de gravações no host (alvo linux): motor real sobre um .tsv, sem hardware.
set(EXTRA_COMPONENT_DIRS ../../components)
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(replay)
