cmake_minimum_required(VERSION 3.25)
project(DAB)

set(CMAKE_CXX_STANDARD 23)

option(DAB_BUILD_PAHO_MQTT "Flag that defines whether the paho.mqtt.c library should be built from sources" ON)

if(${DAB_BUILD_PAHO_MQTT})
    add_subdirectory(libs/paho-mqtt-c)
else()
    find_package(eclipse-paho-mqtt-c CONFIG REQUIRED)
endif()

add_executable(DAB dab.cpp
                Json.h
                dabBridge.h
                dabClient.h
                dabMqttInterface.h)


target_link_libraries(DAB PRIVATE eclipse-paho-mqtt-c::paho-mqtt3c-static)
