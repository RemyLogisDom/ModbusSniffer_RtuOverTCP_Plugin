#ifndef LOGISDOMINTERFACE_H
#define LOGISDOMINTERFACE_H

#include <QString>
#include <QWidget>
#include <QTcpSocket>


class LogisDomInterface
{
public:
    virtual ~LogisDomInterface() {}
    virtual QObject *getObject () = 0;
    virtual QWidget *widgetUi() = 0;
    virtual QWidget *getDevWidget(QString RomID) = 0;
    virtual void setConfigFileName(const QString) = 0;
    virtual void readDevice(const QString &device) = 0;
    virtual QString getDeviceCommands(const QString &device) = 0;
    virtual void saveConfig() = 0;
    virtual void readConfig() = 0;
    virtual void setLockedState(bool) = 0;
    virtual QString getDeviceConfig(QString) = 0;
    virtual void setDeviceConfig(const QString &, const QString &) = 0;
    virtual QString getName() = 0;
    virtual void setStatus(const QString) = 0;
    virtual bool acceptCommand(const QString) = 0;
    virtual bool isDimmable(const QString) = 0;
    virtual bool isManual(const QString) = 0;
    virtual double getMaxValue(const QString) = 0;
signals:
    virtual void newDeviceValue(QString, QString) = 0;
    virtual void newDevice(LogisDomInterface*, QString) = 0;
    virtual void deviceSelected(QString) = 0;
    virtual void updateInterfaceName(LogisDomInterface*, QString) = 0;
    virtual void connectionStatus(QAbstractSocket::SocketState) = 0;
};


QT_BEGIN_NAMESPACE

#define LogisDomInterface_iid "logisdom.network.LogisDomInterface/1.0"

Q_DECLARE_INTERFACE(LogisDomInterface, LogisDomInterface_iid)
QT_END_NAMESPACE

#endif

