cmake_minimum_required(VERSION 3.16)
project(QtAutoAgent VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Qml Gui Quick QuickControls2 Widgets WebChannel WebSockets)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../webchannel_proxy" "${CMAKE_CURRENT_BINARY_DIR}/proxy_sdk")

add_library(QtAutoAgent SHARED main.cpp)
target_include_directories(QtAutoAgent PRIVATE 
    ${TOOL_PATH}/include/detours
)

target_link_libraries(QtAutoAgent 
    PRIVATE 
    Qt5::Core 
    Qt5::Qml
    Qt5::Gui
    Qt5::Quick
    Qt5::QuickControls2 
    Qt5::Widgets 
    Qt5::WebChannel 
    Qt5::WebSockets
    webchannel_proxy
    "${TOOL_PATH}/lib/detours.lib"
)

set_target_properties(QtAutoAgent PROPERTIES PREFIX "")