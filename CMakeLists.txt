cmake_minimum_required(VERSION 3.16)
project(ModbusSnifferPlugin VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Gui Network Widgets Xml)

qt_standard_project_setup()

qt_add_plugin(ModbusSnifferPlugin)
target_sources(ModbusSnifferPlugin PRIVATE
    ModbusSniffer.ui
    ModbusSnifferPlugin.cpp ModbusSnifferPlugin.h modbus_rtu_core.hpp tcpworker.h
)
target_link_libraries(ModbusSnifferPlugin PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::Widgets
    Qt::Xml
)


# Resources:
set(Modbus_resource_files
    "../images/connecting.png"
    "../images/disconnected.png"
    "../images/disconnecting.png"
    "../images/pause.png"
    "../images/play.png"
)

qt_add_resources(ModbusSnifferPlugin "ModbusSniffer"
    PREFIX
        "/"
    FILES
        ${Modbus_resource_files}
)

install(TARGETS ModbusSnifferPlugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
