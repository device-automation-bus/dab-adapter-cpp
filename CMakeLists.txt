cmake_minimum_required(VERSION 3.25)
project(DAB)

set(CMAKE_CXX_STANDARD 23)
set(PAHO_BUILD_STATIC TRUE)

add_executable(DAB dab.cpp
                Json.h
                dabBridge.h
                dabClient.h
                dabMqttInterface.h)

find_package(eclipse-paho-mqtt-c CONFIG REQUIRED)

target_link_libraries(DAB PRIVATE eclipse-paho-mqtt-c::paho-mqtt3a-static eclipse-paho-mqtt-c::paho-mqtt3c-static eclipse-paho-mqtt-c::paho-mqtt3as-static eclipse-paho-mqtt-c::paho-mqtt3cs-static)
