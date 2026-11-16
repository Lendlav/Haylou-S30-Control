cmake_minimum_required(VERSION 3.16)
project(HaylouS30Control LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
find_library(BLUETOOTH_LIBRARY NAMES bluetooth)
if(NOT BLUETOOTH_LIBRARY)
    message(FATAL_ERROR "BlueZ development files are required (install libbluetooth-dev).")
endif()

add_executable(HaylouS30Control src/main.cpp)
target_link_libraries(HaylouS30Control PRIVATE Qt6::Widgets ${BLUETOOTH_LIBRARY})
target_compile_options(HaylouS30Control PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS HaylouS30Control RUNTIME DESTINATION bin)
