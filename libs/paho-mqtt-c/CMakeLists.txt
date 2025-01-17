# Copyright 2024 Amazon.com, Inc. or its affiliates.
# Copyright 2024 Netflix Inc.
# Copyright 2024 Google LLC
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

include(ExternalProject)

set(PAHO_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/install)
set(PAHO_LIBRARIES
    paho-mqtt3c
    paho-mqtt3cs
    paho-mqtt3a
    paho-mqtt3as
)

file(MAKE_DIRECTORY ${PAHO_INSTALL_PREFIX}/include)

set(PAHO_BYPRODUCTS)
foreach(PAHO_LIB ${PAHO_LIBRARIES})
    set(PAHO_LIB_PATH ${PAHO_INSTALL_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${PAHO_LIB}${CMAKE_STATIC_LIBRARY_SUFFIX})
    set(PAHO_TARGET_NAME eclipse-paho-mqtt-c::${PAHO_LIB}-static)
    list(APPEND PAHO_BYPRODUCTS ${PAHO_LIB_PATH})
    add_library(${PAHO_TARGET_NAME} STATIC IMPORTED GLOBAL)
    set_target_properties(${PAHO_TARGET_NAME} PROPERTIES
        IMPORTED_LOCATION ${PAHO_LIB_PATH}
        INTERFACE_INCLUDE_DIRECTORIES "${PAHO_INSTALL_PREFIX}/include"
    )
    add_dependencies(${PAHO_TARGET_NAME} paho_mqtt_c_external)
endforeach()

ExternalProject_Add(
    paho_mqtt_c_external
    GIT_REPOSITORY https://github.com/eclipse-paho/paho.mqtt.c
    GIT_TAG v1.3.14
    UPDATE_DISCONNECTED true
    CMAKE_ARGS
        -DPAHO_WITH_SSL=ON
        -DPAHO_WITH_LIBUUID=OFF
        -DPAHO_BUILD_SHARED=OFF
        -DPAHO_BUILD_STATIC=ON
        -DPAHO_BUILD_DOCUMENTATION=OFF
        -DPAHO_BUILD_SAMPLES=OFF
        -DPAHO_BUILD_DEB_PACKAGE=OFF
        -DPAHO_ENABLE_TESTING=OFF
        -DPAHO_ENABLE_CPACK=OFF
        -DPAHO_HIGH_PERFORMANCE=ON
        -DPAHO_USE_SELECT=OFF
        -DCMAKE_INSTALL_PREFIX=${PAHO_INSTALL_PREFIX}
    BUILD_BYPRODUCTS
        ${PAHO_BYPRODUCTS}
)
