cmake_minimum_required(VERSION 3.16)
project(qt_webchannel_proxy_sdk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Quick WebChannel WebSockets)

add_library(qt_webchannel_proxy_sdk
    src/UiAutomationProxyServer.cpp
    include/UiAutomationProxyServer.h
)
target_include_directories(qt_webchannel_proxy_sdk PUBLIC include)
target_link_libraries(qt_webchannel_proxy_sdk
    PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::WebChannel
    Qt6::WebSockets
)
