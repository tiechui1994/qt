cmake_minimum_required(VERSION 3.16)
project(qml VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets WebChannel WebSockets)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../qt_webchannel_proxy_sdk" "${CMAKE_CURRENT_BINARY_DIR}/proxy_sdk")

add_executable(qml WIN32 main.cpp qml.qrc)
target_link_libraries(qml
    PRIVATE
    qt_webchannel_proxy_sdk
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Qml
    Qt5::Quick
    Qt5::QuickControls2
)