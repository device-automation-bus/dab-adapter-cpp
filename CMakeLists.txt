cmake_minimum_required(VERSION 3.26)
project(DAB)

set(CMAKE_CXX_STANDARD 23)
set(PAHO_BUILD_STATIC TRUE)

add_executable( DAB dab.cpp
                Json.h
                dabBridge.h
                dabClient.h
                dabMqttInterface.h
                rdkAdapter.h)

# Prefer static paho from vcpkg, otherwise use the system-provided library
find_package(eclipse-paho-mqtt-c CONFIG REQUIRED)
if(TARGET eclipse-paho-mqtt-c::paho-mqtt3cs-static)
  set(PAHO_LIBS eclipse-paho-mqtt-c::paho-mqtt3cs-static)
else()
  include(FindOpenSSL)
  set(PAHO_LIBS eclipse-paho-mqtt-c::paho-mqtt3cs)
endif()

# Prefer static libuuid from vcpkg, otherwise use the system-provided library
find_package(unofficial-libuuid QUIET)
if (unofficial-libuuid_FOUND)
  set(UUID_LIBS unofficial::UUID::uuid)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(UUID REQUIRED uuid)
  set(UUID_LIBS ${UUID_LIBRARIES})
endif()

find_package(Boost REQUIRED)
find_package(CURL REQUIRED)

target_link_libraries(DAB PRIVATE ${PAHO_LIBS} CURL::libcurl ${UUID_LIBS})
