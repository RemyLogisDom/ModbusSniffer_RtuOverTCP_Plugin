TEMPLATE    = lib
CONFIG     += plugin
QT         += widgets network
HEADERS     = ModbusSnifferPlugin.h tcpworker.h modbus_rtu_core.hpp
SOURCES     = ModbusSnifferPlugin.cpp
TARGET      = ModbusSnifferPlugin
FORMS       += ModbusSniffer.ui
#DESTDIR         = ../../build-LogisDom-Desktop_Qt_5_15_2_GCC_64bit-Release/release/plugins
DESTDIR         = ../build-LogisDom-Desktop_Qt_6_7_3_GCC_64bit-Debug/release/plugins
QT += network core gui xml widgets

RESOURCES += \
    ModbusSniffer.qrc

