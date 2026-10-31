TEMPLATE        = lib
CONFIG         += plugin
QT             += widgets network
HEADERS         = ModbusPlugin.h
SOURCES         = ModbusPlugin.cpp
TARGET          = ModbusPlugin
#DESTDIR         = ../../build-LogisDom_Ubuntu64_Qt51210-Desktop_Qt_5_12_10_GCC_64bit-Release/release/plugins
FORMS += Modbus.ui
QT += network core gui xml widgets

RESOURCES += \
    Modbus.qrc

