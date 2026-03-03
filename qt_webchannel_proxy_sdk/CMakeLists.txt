cmake_minimum_required(VERSION 3.16)
project(qt_webchannel_proxy_sdk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)


find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Quick WebChannel WebSockets)

add_library(qt_webchannel_proxy_sdk
    src/UiAutomationProxyServer.cpp
    src/UiAutomationProxyServerQml.cpp
    include/UiAutomationProxyServer.h
)
target_include_directories(qt_webchannel_proxy_sdk PUBLIC include)
target_link_libraries(qt_webchannel_proxy_sdk
    PUBLIC
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Quick
    Qt5::WebChannel
    Qt5::WebSockets
)
