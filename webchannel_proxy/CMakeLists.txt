cmake_minimum_required(VERSION 3.16)
project(webchannel_proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)


find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Quick WebChannel WebSockets)

add_library(webchannel_proxy
    src/UiAutomationProxyServer.cpp
    src/QtGenericUiAutomationHandler.cpp
    src/QtQmlUiAutomationHandler.cpp
    include/UiAutomationProxyServer.h
)
target_include_directories(webchannel_proxy PUBLIC include)
target_link_libraries(webchannel_proxy
    PUBLIC
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Quick
    Qt5::WebChannel
    Qt5::WebSockets
)
