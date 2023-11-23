cmake_minimum_required(VERSION 3.26)
project(DAB)

set(CMAKE_CXX_STANDARD 23)
set(PAHO_BUILD_STATIC TRUE)
set(CMAKE_PREFIX_PATH vcpkg_installed/x64-osx)

add_executable( DAB dab.cpp
                Json.h
                dabBridge.h
                dabClient.h
                dabMqttInterface.h)

find_package(eclipse-paho-mqtt-c CONFIG REQUIRED)
if(TARGET eclipse-paho-mqtt-c::paho-mqtt3a-static)
set(PAHO_LIBS eclipse-paho-mqtt-c::paho-mqtt3a-static eclipse-paho-mqtt-c::paho-mqtt3c-static eclipse-paho-mqtt-c::paho-mqtt3as-static eclipse-paho-mqtt-c::paho-mqtt3cs-static)
else()
include(FindOpenSSL)
set(PAHO_LIBS eclipse-paho-mqtt-c::paho-mqtt3a eclipse-paho-mqtt-c::paho-mqtt3c eclipse-paho-mqtt-c::paho-mqtt3as eclipse-paho-mqtt-c::paho-mqtt3cs)
endif()

target_link_libraries(DAB PRIVATE ${PAHO_LIBS})
