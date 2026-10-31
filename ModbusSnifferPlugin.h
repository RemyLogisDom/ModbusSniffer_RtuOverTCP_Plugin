#ifndef MODBUSSNIFFERPLUGIN_H
#define MODBUSSNIFFERPLUGIN_H

#include <QObject>
#include <QComboBox>
#include <QtPlugin>
#include <QTcpSocket>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <QMutexLocker>

#include "ui_ModbusSniffer.h"
#include "../interface.h"
#include "modbus_rtu_core.hpp"
#include "tcpworker.h"

#ifdef Q_OS_WIN32
#include "windows.h"
#include "Winsock2.h"
#endif

#ifdef Q_OS_LINUX
#include "sys/socket.h"
#endif

static const char STX = 0x02;			// Frame Start
static const char ETX = 0x03;			// Frame End
static const char EOT = 0x04;			// Frame Interrupted
static const char LF = 0x0A;			// Line Feed
static const char SP = 0x20;			// Space
static const char CR = 0x0D;			// Carriage Return
static const int DataTimeOut = 60000;


enum NetTraffic
{
    Connecting, Waitingforanswer, Disconnected, Connected, Paused
};


/* Table of CRC values for high-order byte */
const quint8 table_crc_hi[256] = {
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40
};

/* Table of CRC values for low-order byte */
const quint8 table_crc_lo[256] = {
    0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06,
    0x07, 0xC7, 0x05, 0xC5, 0xC4, 0x04, 0xCC, 0x0C, 0x0D, 0xCD,
    0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
    0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A,
    0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC, 0x14, 0xD4,
    0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
    0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3,
    0xF2, 0x32, 0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4,
    0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
    0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29,
    0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF, 0x2D, 0xED,
    0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
    0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60,
    0x61, 0xA1, 0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67,
    0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
    0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68,
    0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA, 0xBE, 0x7E,
    0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
    0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71,
    0x70, 0xB0, 0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92,
    0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
    0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B,
    0x99, 0x59, 0x58, 0x98, 0x88, 0x48, 0x49, 0x89, 0x4B, 0x8B,
    0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
    0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42,
    0x43, 0x83, 0x41, 0x81, 0x80, 0x40
};

//enum registerType { coil = 0, discreteInput, inputRegister, holdingRegister };
enum registerType { inputRegister = 0, holdingRegister };
enum registerTypeDef { uint16b = 0, int16b, uint32bMSDFirst, uint32bMSDLast };
enum readInterval { readConstant, read1mn, read2mn, read5mn, read10mn, read30mn, read1hour };

class comboType : public QComboBox
{
    Q_OBJECT
public:
    comboType() {
        addItem("holding");
        addItem("input");
        //addItem("coil");
        //addItem("Discrete Input");
    }
};

class comboInterval : public QComboBox
{
    Q_OBJECT
public:
    comboInterval() {
        addItem("Constant");
        addItem("1mn");
        addItem("2mn");
        addItem("5mn");
        addItem("10mn");
        addItem("30mn");
        addItem("1 hour");
    }
};

class comboTypedef : public QComboBox
{
    Q_OBJECT
public:
    comboTypedef() {
        addItem("16b unsigned");
        addItem("16b signed");
        addItem("32b MSD First");
        addItem("32b MSD Last");
    }
};


enum FunctionCode {
    Invalid = 0x00,
    ReadCoils = 0x01,
    ReadDiscreteInputs = 0x02,
    ReadHoldingRegisters = 0x03,
    ReadInputRegisters = 0x04,
    WriteSingleCoil = 0x05,
    WriteSingleRegister = 0x06,
    ReadExceptionStatus = 0x07,
    Diagnostics = 0x08,
    GetCommEventCounter = 0x0B,
    GetCommEventLog = 0x0C,
    WriteMultipleCoils = 0x0F,
    WriteMultipleRegisters = 0x10,
    ReportServerId = 0x11,
    ReadFileRecord = 0x14,
    WriteFileRecord = 0x15,
    MaskWriteRegister = 0x16,
    ReadWriteMultipleRegisters = 0x17,
    ReadFifoQueue = 0x18,
    EncapsulatedInterfaceTransport = 0x2B,
    ReadErrorCode = 0x84,
    WriteErrorCode = 0x86,
    UndefinedFunctionCode = 0x100
};

struct modbusItem {
    QString RomID;
    int id = 0;
    QTableWidgetItem *slave, *address, *maxValue;
    comboType *type;
    comboInterval *readInterval;
    comboTypedef *typeDef;
    QTableWidgetItem *value, *lastRead;
    modbusItem(QString romid) : RomID(romid)
    {
        bool ok;
        id = RomID.right(5).left(3).toInt(&ok);
        slave = new QTableWidgetItem("1");
        address = new QTableWidgetItem("1");
        type = new comboType;
        readInterval = new comboInterval;
        typeDef = new comboTypedef;
        value = new QTableWidgetItem();
        value->setFlags(value->flags() ^ Qt::ItemIsEditable);
        lastRead = new QTableWidgetItem();
        lastRead->setFlags(lastRead->flags() ^ Qt::ItemIsEditable);
        type->setCurrentIndex(0);
        readInterval->setCurrentIndex(0);
        maxValue = new QTableWidgetItem("65535");
    }
    bool operator==(const modbusItem& dev)
    {
        bool ok;
        // Check if slave are equal
        int S1 = slave->text().toInt(&ok);
        if (!ok) return false;
        int S2 = dev.slave->text().toInt(&ok);
        if (!ok) return false;
        if (S1 != S2) return false;

        // Check if request type are the same
        if (type->currentIndex() != dev.type->currentIndex()) return false;

        // Set the size of the first device
        int s = 1;
        switch (typeDef->currentIndex()) {
        case uint16b : s = 1; break;
        case int16b : s = 1; break;
        case uint32bMSDFirst : s = 2; break;
        case uint32bMSDLast : s = 2; break; }

        // Check if address are consecutive
        //int A1 = address->text().toInt(&ok) + 1;
        int A1 = address->text().toInt(&ok) + s;
        if (!ok) return false;
        int A2 = dev.address->text().toInt(&ok);
        if (!ok) return false;
        if (A1 != A2) return false;

        return true;
    }
};

struct modbusReadRequest {
    modbusReadRequest(quint16 Slave, quint16 Type, quint16 Address, quint16 DataSize) : slave(Slave), type(Type), address(Address), size(DataSize)
    {
        switch (Type) {
        case 0 : type = ReadHoldingRegisters; break;
        case 1 : type = ReadInputRegisters; break;
        }
    }
    quint16 slave;
    quint16 type;
    quint16 address;
    quint16 size;
    QByteArray buffer;
    bool operator==(const modbusReadRequest& req)
    {
        bool result = (slave == req.slave) && (type == req.type) && (address == req.address) && (size == req.size);
        return result;
    }
};



class ModbusSnifferPlugin : public QWidget, LogisDomInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "logisdom.network.LogisDomInterface/1.0" FILE "ModbusSnifferPlugin.json")
    Q_INTERFACES(LogisDomInterface)
public:
    ModbusSnifferPlugin( QWidget *parent = nullptr );
    ~ModbusSnifferPlugin() override;
    QObject* getObject() override;
    QWidget *widgetUi() override;
    QWidget *getDevWidget(QString RomID) override;
    void setConfigFileName(const QString) override;
    void readDevice(const QString &device) override;
    QString getDeviceCommands(const QString &device) override;
    void saveConfig() override;
    void readConfig() override;
    void setLockedState(bool) override;
    QString getDeviceConfig(QString) override;
    QString getName() override;
    void setDeviceConfig(const QString &, const QString &) override;
    void setStatus(const QString) override;
    bool acceptCommand(const QString) override;
    bool isDimmable(const QString) override;
    bool isManual(const QString) override;
    double getMaxValue(const QString) override;
    QTimer readTimer;
    int idle = 0;
    bool lockedState = false;
    uint16_t calcCRC16(QByteArray s);
    QMap<QString, int> m_registres;
signals:
    void newDeviceValue(QString, QString) override;
    void newDevice(LogisDomInterface*, QString) override;
    void deviceSelected(QString) override;
    void updateInterfaceName(LogisDomInterface*, QString) override;
    void connectionStatus(QAbstractSocket::SocketState) override;
private:
    QWidget *ui;
    QWidget *devUi;
    QString configFileName;
    QIcon tcpIconUnconnectedState, tcpIconHostLookupState, tcpIconConnectingState, tcpIconConnectedState, tcpIconClosingState;
    QVector<modbusItem*> modbusDevices;
    void setipaddress(const QString &adr);
    void setport(const QString &adr);
    QString ip2Hex(const QString &ip);
    QString buildRomID(int n);
    Ui::ModbusUI *mui;
    QHash<QString, QTableWidgetItem *> m_cellValeur;
    QHash<QString, QTableWidgetItem *> m_cellHex;
    QHash<QString, QTableWidgetItem *> m_cellSource;
    QHash<QString, QTableWidgetItem *> m_cellHeure;
    QHash<QString, QTableWidgetItem *> w_cellValeur;
    QHash<QString, QTableWidgetItem *> w_cellHex;
    QHash<QString, QTableWidgetItem *> w_cellSource;
    QHash<QString, QTableWidgetItem *> w_cellHeure;
    QString lastStatus;
    void log(const QString);
    void logWr(const QString);
    QString logStr;
    QString m_ip;
    quint16 m_port = 1470;
    QThread *m_thread = nullptr;
    TcpWorker *m_worker = nullptr;
    void startWorker();
    void stopWorker();
    QStringList Paramters, Values;
    bool isIpValid(QString str);
    bool isPortValid(QString str);
    void updateIpRomID();
    void sortDevicesList();
    void sortDevicesId();
    modbusItem *getRomID(quint8 slave, quint16 address, QString zone);
    modbusItem *getRomID(quint8 slave, quint16 address, registerType zone);
    modbusItem *addDevice(QString id = "");
    int typeToSize(int type);
    int lastMinute = -1;
private slots:
    void registreMisAJour(quint8 esclaveAdresse, const QString &zone, quint16 adresse, quint16 valeur, const QString &source);
    void tcpStatusChange(int);
    void logTCP(QString);
    void logWrTCP(QString);
    void showLog();
    void on_AddButton_clicked();
    void on_AddRegisterButton_clicked();
    void on_RemoveButton_clicked();
    void editIP_editingFinished();
    void on_editPort_editingFinished();
    void on_ClearLogButton_clicked();
    void displayDevice(int, int);
    void checkCombo();
    void on_editName_editingFinished();
};


#endif

