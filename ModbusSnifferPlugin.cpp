#include <QtWidgets>
#include <QTcpSocket>


#include <QtCore/qmath.h>
#include <QtCore/qpointer.h>
#include <QtCore/qqueue.h>
#include <QtCore/qtimer.h>
#include "ModbusSnifferPlugin.h"
#include "../common.h"


ModbusSnifferPlugin::ModbusSnifferPlugin(QWidget *parent) : QWidget(parent)
{
    ui = new QWidget();
    QGridLayout *layout = new QGridLayout(ui);
    QWidget *w = new QWidget();
    mui = new Ui::ModbusUI;
    mui->setupUi(w);
    layout->addWidget(w);
    mui->editName->setToolTip("ModbusPlugin");
    mui->deviceTable->setColumnCount(8);
    mui->deviceTable->setHorizontalHeaderItem(0, new QTableWidgetItem("RomID"));
    mui->deviceTable->setHorizontalHeaderItem(1, new QTableWidgetItem("Slave"));
    mui->deviceTable->setHorizontalHeaderItem(2, new QTableWidgetItem("Address"));
    mui->deviceTable->setHorizontalHeaderItem(3, new QTableWidgetItem("Type"));
    mui->deviceTable->setHorizontalHeaderItem(4, new QTableWidgetItem("Resolution"));
    //mui->deviceTable->setHorizontalHeaderItem(5, new QTableWidgetItem("Read Interval"));
    mui->deviceTable->setHorizontalHeaderItem(5, new QTableWidgetItem("Value"));
    mui->deviceTable->setHorizontalHeaderItem(6, new QTableWidgetItem("Last Read"));
    mui->deviceTable->setHorizontalHeaderItem(7, new QTableWidgetItem("Max Value"));

    mui->m_table->setColumnCount(6);
    mui->m_table->setHorizontalHeaderLabels( { "Esclave", "Adresse", "Type", "Valeur", "Hex", "Source / Heure" });
    mui->m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);   // lecture seule
    mui->m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    mui->m_table->setAlternatingRowColors(true);
    mui->m_table->verticalHeader()->setVisible(false);
    mui->m_table->horizontalHeader()->setStretchLastSection(true);
    mui->m_table->setSortingEnabled(true);          // clic sur l'en-tête pour trier

    mui->w_table->setColumnCount(6);
    mui->w_table->setHorizontalHeaderLabels( { "Esclave", "Adresse", "Type", "Valeur", "Hex", "Source / Heure" });
    mui->w_table->setEditTriggers(QAbstractItemView::NoEditTriggers);   // lecture seule
    mui->w_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    mui->w_table->setAlternatingRowColors(true);
    mui->w_table->verticalHeader()->setVisible(false);
    mui->w_table->horizontalHeader()->setStretchLastSection(true);
    mui->w_table->setSortingEnabled(true);          // clic sur l'en-tête pour trier

    connect(mui->AddButton, SIGNAL(clicked()), this, SLOT(on_AddButton_clicked()));
    connect(mui->AddRegisterButton, SIGNAL(clicked()), this, SLOT(on_AddRegisterButton_clicked()));
    connect(mui->RemoveButton, SIGNAL(clicked()), this, SLOT(on_RemoveButton_clicked()));
    connect(mui->pushButtonClearLog, SIGNAL(clicked()), this, SLOT(on_ClearLogButton_clicked()));
    connect(mui->editIP, SIGNAL(editingFinished()), this, SLOT(editIP_editingFinished()));
    connect(mui->editPort, SIGNAL(editingFinished()), this, SLOT(on_editPort_editingFinished()));
    connect(mui->editName, SIGNAL(editingFinished()), this, SLOT(on_editName_editingFinished()));
    connect(mui->deviceTable->itemDelegate(),SIGNAL(closeEditor(QWidget*,QAbstractItemDelegate::EndEditHint)),SLOT(checkCombo()));
    connect(mui->deviceTable, SIGNAL(cellPressed(int, int)),SLOT(displayDevice(int, int)));
    connect(&readTimer, SIGNAL(timeout()),SLOT(readAllNow()));
    readTimer.start(1000);
    mui->RemoveButton->setEnabled(false);


    QObject::connect(mui->deviceTable->verticalHeader(), &QHeaderView::sectionClicked, [this]() {
        mui->RemoveButton->setEnabled(true);
    });

    QObject::connect(mui->Start, &QPushButton::clicked, [this]() {
        startWorker();
    });

    QObject::connect(mui->Stop, &QPushButton::clicked, [this]() {
        log("Stop requested");
        stopWorker();
    });

    tcpIconUnconnectedState.addPixmap(QPixmap(QString::fromUtf8(":/images/images/disconnected.png")), QIcon::Normal, QIcon::Off);
    tcpIconHostLookupState.addPixmap(QPixmap(QString::fromUtf8(":/images/images/connecting.png")), QIcon::Normal, QIcon::Off);
    tcpIconConnectingState.addPixmap(QPixmap(QString::fromUtf8(":/images/images/connecting.png")), QIcon::Normal, QIcon::Off);
    tcpIconConnectedState.addPixmap(QPixmap(QString::fromUtf8(":/images/images/connected.png")), QIcon::Normal, QIcon::Off);
    tcpIconClosingState.addPixmap(QPixmap(QString::fromUtf8(":/images/images/disconnecting.png")), QIcon::Normal, QIcon::Off);
}

ModbusSnifferPlugin::~ModbusSnifferPlugin()
{
    stopWorker();
}


void ModbusSnifferPlugin::startWorker()
{
    if (m_thread) return;   // déjà démarré
    m_thread = new QThread(this);
    m_worker = new TcpWorker;   // pas de parent : il est déplacé dans m_thread
    m_worker->setEndpoint(m_ip, m_port);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &TcpWorker::start);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &TcpWorker::tcpStatusChange, this, &ModbusSnifferPlugin::tcpStatusChange);
    connect(m_worker, &TcpWorker::logTCP, this, &ModbusSnifferPlugin::logTCP);
    connect(m_worker, &TcpWorker::registreMisAJour, this, &ModbusSnifferPlugin::registreMisAJour);
    m_thread->start();
    log(QString("Connection requested to %1:%2").arg(m_ip).arg(m_port));
}


void ModbusSnifferPlugin::stopWorker()
{
    if (!m_thread) return;
    // Bloquant : au retour, le socket est fermé et le timer arrêté dans le thread du worker
    QMetaObject::invokeMethod(m_worker, &TcpWorker::stop, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();   // le worker est détruit par deleteLater à la fin du thread
    delete m_thread;
    m_thread = nullptr;
    m_worker = nullptr;
}

QObject *ModbusSnifferPlugin::getObject()
{
    return this;
}

QWidget *ModbusSnifferPlugin::widgetUi()
{
    return ui;
}

QWidget *ModbusSnifferPlugin::getDevWidget(QString)
{
    return nullptr;
}


void ModbusSnifferPlugin::setConfigFileName(const QString fileName)
{
    configFileName = fileName;
    mui->labelInterfaceName->setToolTip(configFileName);
}

void ModbusSnifferPlugin::readDevice(const QString &)
{
}

QString ModbusSnifferPlugin::getDeviceCommands(const QString &)
{
    return "";
}



void ModbusSnifferPlugin::saveConfig()
{
    QFile file(configFileName);
    QByteArray configdata;
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setGenerateByteOrderMark(true);
        QString str;
        QDateTime now = QDateTime::currentDateTime();
        str += "Configuration file " + now.toString() + "\n";
        str += saveformat("IPadress", m_ip);
        str += saveformat("Port", QString("%1").arg(m_port));
        str += saveformat("Name", mui->editName->text());
        sortDevicesId();
        int index = 1;
        foreach (modbusItem *dev, modbusDevices) {
            QString data;
            data.append(dev->RomID + "/");    // RomID
            data.append(dev->slave->text() + "/");    // Slave
            data.append(dev->address->text() + "/");    // Address
            data.append(dev->type->currentText() + "/");    // Type
            data.append(dev->typeDef->currentText() + "/");  // TypeDef
            data.append(dev->readInterval->currentText() + "/");    // Read Interval
            data.append(dev->maxValue->text());    // Max Value
            str += saveformat(QString("Device_%1").arg(index++, 3, 10, QChar('0')), data); }
        out << str;
        file.close(); }
}


void ModbusSnifferPlugin::readConfig()
{
    QFile file(configFileName);
    QByteArray configdata;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream in(&file);
        QString configData;
        configData.append(in.readAll());
        file.close();
        setipaddress(getvalue("IPadress", configData));
        setport(getvalue("Port", configData));
        int count = 1;
        QString devID = QString("Device_%1").arg(count, 3, 10, QChar('0'));
        QString data = getvalue(devID, configData);
        QString Name = getvalue("Name", configData);
        if (!Name.isEmpty()) mui->editName->setText(Name);
        emit(updateInterfaceName(this, mui->editName->text()));
        while (!data.isEmpty())
        {
            QStringList parameters = data.split("/");
            if (parameters.count() == 6) {
                modbusItem *newDev = addDevice(parameters.at(0));
                if (newDev) {
                    newDev->RomID = parameters.at(0);
                    newDev->slave->setText(parameters.at(1));
                    newDev->address->setText(parameters.at(2));
                    newDev->type->setCurrentText(parameters.at(3));
                    newDev->typeDef->setCurrentText(parameters.at(4));
                    newDev->readInterval->setCurrentText(parameters.at(5));
                    newDev->maxValue->setText("65535");
                }
            }
            if (parameters.count() == 7) {
                modbusItem *newDev = addDevice(parameters.at(0));
                if (newDev) {
                    newDev->RomID = parameters.at(0);
                    newDev->slave->setText(parameters.at(1));
                    newDev->address->setText(parameters.at(2));
                    newDev->type->setCurrentText(parameters.at(3));
                    newDev->typeDef->setCurrentText(parameters.at(4));
                    newDev->readInterval->setCurrentText(parameters.at(5));
                    newDev->maxValue->setText(parameters.at(6));
                }
            }
            devID = QString("Device_%1").arg(++count, 3, 10, QChar('0'));
            data = getvalue(devID, configData);
        }
        sortDevicesList();
    }
    startWorker();
}




void ModbusSnifferPlugin::setLockedState(bool state)
{
    lockedState = state;
    mui->AddButton->setEnabled(!state);
    mui->RemoveButton->setEnabled(!state);
    mui->editIP->setEnabled(!state);
    mui->editPort->setEnabled(!state);
    mui->editName->setEnabled(!state);
    //mui->checkBoxWr10->setEnabled(!state);
    for (int n=0; n<mui->deviceTable->rowCount(); n++) {
        for (int i=1; i<3; i++) {
        QTableWidgetItem *item = mui->deviceTable->item(n, i);
        if (item) {
        if (state) item->setFlags(item->flags() ^ Qt::ItemIsEditable);
        else item->setFlags(item->flags() | Qt::ItemIsEditable); } }

        QTableWidgetItem *item = mui->deviceTable->item(n, 8);
        if (item) {
        if (state) item->setFlags(item->flags() ^ Qt::ItemIsEditable);
        else item->setFlags(item->flags() | Qt::ItemIsEditable); } }

        foreach (modbusItem *dev, modbusDevices) {
        dev->type->setEnabled(!state);
        dev->typeDef->setEnabled(!state);
        dev->readInterval->setEnabled(!state);
    }
}

QString ModbusSnifferPlugin::getDeviceConfig(QString)
{
    return "";
}

void ModbusSnifferPlugin::setDeviceConfig(const QString &, const QString &)
{
}


QString ModbusSnifferPlugin::getName()
{
    return mui->editName->text();
}


double ModbusSnifferPlugin::getMaxValue(const QString RomID)
{
    foreach (modbusItem *dev, modbusDevices) {
        if (dev->RomID == RomID) {
            bool ok;
            int max = dev->maxValue->text().toInt(&ok);
            if (ok) return max;
        } }
    return 65535;
}



bool ModbusSnifferPlugin::isManual(const QString)
{
    return false;
}

bool ModbusSnifferPlugin::isDimmable(const QString)
{
    return true;
}

bool ModbusSnifferPlugin::acceptCommand(const QString)
{
    return true;
}


uint16_t ModbusSnifferPlugin::calcCRC16(QByteArray s) // version 1.04d suppresion static calcCRC16
{
    quint8 crc_hi = 0xFF;
    quint8 crc_lo = 0xFF;
    unsigned int i;
    foreach (uint8_t t, s)
    {
        i = crc_hi ^ t;
        crc_hi = crc_lo ^ table_crc_hi[i];
        crc_lo = table_crc_lo[i];
    }
    quint16 result = quint16(crc_hi << 8 | crc_lo);
    return result;
}

void ModbusSnifferPlugin::setStatus(const QString)
{
}

void ModbusSnifferPlugin::setipaddress(const QString &adr)
{
    if (isIpValid(adr))
    {
        mui->editIP->setText(adr);
        m_ip = adr;
    }
}

void ModbusSnifferPlugin::setport(const QString &p)
{
    if (!p.isEmpty()) {
        mui->editPort->setText(p);		// affichage de l'adresse IP
        m_port = quint16(p.toInt()); }
    else {
        mui->editPort->setText("1470");		// affichage de l'adresse IP
        m_port = quint16(1470); }
}



void ModbusSnifferPlugin::tcpStatusChange(int state)
{
    switch (state)
    {
        case QAbstractSocket::UnconnectedState : mui->toolButtonTcpState->setIcon(tcpIconUnconnectedState);
            log(QString("Disconnected"));
            mui->toolButtonTcpState->setToolTip(tr("Not connected"));
        break;
        case QAbstractSocket::HostLookupState : mui->toolButtonTcpState->setIcon(tcpIconConnectingState);
            log(QString("Host lookup"));
            mui->toolButtonTcpState->setToolTip(tr("Host lookup"));
        break;
        case QAbstractSocket::ConnectingState : mui->toolButtonTcpState->setIcon(tcpIconConnectingState);
            log(QString("Connecting"));
            mui->toolButtonTcpState->setToolTip(tr("Connecting"));
        break;
        case QAbstractSocket::ConnectedState : mui->toolButtonTcpState->setIcon(tcpIconConnectedState);
            log(QString("Connected"));
            mui->toolButtonTcpState->setToolTip(tr("Connected"));
        break;
        case QAbstractSocket::ClosingState : mui->toolButtonTcpState->setIcon(tcpIconClosingState);
            log(QString("Disconnecting"));
            mui->toolButtonTcpState->setToolTip(tr("Disconnecting"));
        break;
        default: break;
    }
}

// Item numérique : s'affiche en nombre ET se trie numériquement
static QTableWidgetItem *itemNum(int n)
{
    auto *it = new QTableWidgetItem;
    it->setData(Qt::DisplayRole, n);          // un int => tri numérique automatique
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

static QTableWidgetItem *itemTexte(const QString &s)
{
    return new QTableWidgetItem(s);
}


void ModbusSnifferPlugin::registreMisAJour(quint8 esclave, const QString &zone, quint16 adresse, quint16 valeur, const QString &source)
{
    const QString cle = QString("%1/%2/%3").arg(esclave).arg(zone).arg(adresse);
    m_registres[cle] = valeur;

    const QString hex  = QString("0x%1").arg(quint16(valeur), 4, 16, QChar('0')).toUpper();
    const QString time = QTime::currentTime().toString("HH:mm:ss.zzz");
    const QString info = source + "  " + time;

    if (source == "ecriture")
    {
        auto itVal = w_cellValeur.constFind(cle);
        if (itVal != w_cellValeur.constEnd()) {
            // Registre déjà affiché : on met juste à jour les cellules
            (*itVal)->setData(Qt::DisplayRole, valeur);
            w_cellHex[cle]->setText(hex);
            w_cellSource[cle]->setText(info);
            return;
        }

        // Nouveau registre : on ajoute une ligne.
        // Le tri doit être coupé pendant l'insertion, sinon la ligne "saute"
        // avant que toutes ses cellules soient remplies.
        const bool triActif = mui->w_table->isSortingEnabled();
        mui->w_table->setSortingEnabled(false);


        const int row = mui->w_table->rowCount();
        mui->w_table->insertRow(row);

        auto *cVal    = itemNum(valeur);
        auto *cHex    = itemTexte(hex);
        auto *cSource = itemTexte(info);

        mui->w_table->setItem(row, 0, itemNum(esclave));
        mui->w_table->setItem(row, 1, itemNum(adresse));
        mui->w_table->setItem(row, 2, itemTexte(zone));
        mui->w_table->setItem(row, 3, cVal);
        mui->w_table->setItem(row, 4, cHex);
        mui->w_table->setItem(row, 5, cSource);

        w_cellValeur[cle] = cVal;
        w_cellHex[cle]    = cHex;
        w_cellSource[cle] = cSource;

        mui->w_table->setSortingEnabled(triActif);
    }

    if (source == "lecture")
    {
        auto itVal = m_cellValeur.constFind(cle);
        if (itVal != m_cellValeur.constEnd()) {
            // Registre déjà affiché : on met juste à jour les cellules
            (*itVal)->setData(Qt::DisplayRole, valeur);
            m_cellHex[cle]->setText(hex);
            m_cellSource[cle]->setText(info);
            modbusItem *dev = getRomID(esclave, adresse, zone);
            if (dev) {
                dev->lastRead->setText(time);
                QString Value = QString("%1").arg(valeur);
                QString RomID = dev->RomID;
                dev->value->setText(Value);
                emit(newDeviceValue(RomID, Value));
            }
            return;
        }

        // Nouveau registre : on ajoute une ligne.
        // Le tri doit être coupé pendant l'insertion, sinon la ligne "saute"
        // avant que toutes ses cellules soient remplies.
        const bool triActif = mui->m_table->isSortingEnabled();
        mui->m_table->setSortingEnabled(false);


        const int row = mui->m_table->rowCount();
        mui->m_table->insertRow(row);

        auto *cVal    = itemNum(valeur);
        auto *cHex    = itemTexte(hex);
        auto *cSource = itemTexte(info);

        mui->m_table->setItem(row, 0, itemNum(esclave));
        mui->m_table->setItem(row, 1, itemNum(adresse));
        mui->m_table->setItem(row, 2, itemTexte(zone));
        mui->m_table->setItem(row, 3, cVal);
        mui->m_table->setItem(row, 4, cHex);
        mui->m_table->setItem(row, 5, cSource);

        m_cellValeur[cle] = cVal;
        m_cellHex[cle]    = cHex;
        m_cellSource[cle] = cSource;

        mui->m_table->setSortingEnabled(triActif);

        modbusItem *dev = getRomID(esclave, adresse, zone);
        if (dev) {
            QString Value = QString("%1").arg(valeur);
            QString RomID = dev->RomID;
            dev->value->setText(Value);
            emit(newDeviceValue(RomID, Value));
        }
    }
}



void ModbusSnifferPlugin::logTCP(QString str)
{
    log(str);
}

void ModbusSnifferPlugin::logWrTCP(QString str)
{
    logWr(str);
}


inline quint32 byteArrayToUint32(const QByteArray &bytes)
{
    bool ok;
    return bytes.toHex().toULong(&ok, 16);
}


inline quint16 byteArrayToUint16(const QByteArray &bytes)
{
    bool ok;
    return bytes.toHex().toUInt(&ok, 16);
}


inline qint16 byteArrayToint16(const QByteArray &bytes)
{
    bool ok;
    return bytes.toHex().toInt(&ok, 16);
}


modbusItem *ModbusSnifferPlugin::getRomID(quint8 slave, quint16 address, QString zone)
{
    bool ok;
    foreach (modbusItem *dev, modbusDevices) {
        if (dev->slave->text().toInt(&ok) == slave)
            if (dev->address->text().toInt(&ok) == address)
                if (dev->type->currentText() == zone)
                    return dev;
    }
    return nullptr;
}



modbusItem *ModbusSnifferPlugin::getRomID(quint8 slave, quint16 address, registerType type)
{
    bool ok;
    QString zone;
    if (type == inputRegister) zone = "input";
    if (type == holdingRegister) zone = "holding";
    foreach (modbusItem *dev, modbusDevices) {
        if (dev->slave->text().toInt(&ok) == slave)
            if (dev->address->text().toInt(&ok) == address)
                if (dev->type->currentText() == zone)
                    return dev;
    }
    return nullptr;
}



QString ModbusSnifferPlugin::buildRomID(int n)
{
    QString portHex = QString("%1").arg(m_port, 4, 16, QLatin1Char('0')).toUpper();
    QString ipHex = ip2Hex(m_ip);
    QString id = QString("%1").arg(n, 3, 10, QLatin1Char('0')).toUpper();
    QString RomID = ipHex +  portHex + id + "MD";
    return RomID;
}

bool ModbusSnifferPlugin::isPortValid(QString str)
{
    bool ok;
    int port = str.toInt(&ok);
    if (!ok | (port < 0) | (port > 65535)) return false;
    return true;
}

bool ModbusSnifferPlugin::isIpValid(QString str)
{
    QStringList p = str.split(".");
    if (p.count() != 4) return false;
    bool ok;
    int n = p.at(0).toInt(&ok);
    if (!ok | (n < 0) | (n > 255)) return false;
    n = p.at(1).toInt(&ok);
    if (!ok | (n < 0) | (n > 255)) return false;
    n = p.at(2).toInt(&ok);
    if (!ok | (n < 0) | (n > 255)) return false;
    n = p.at(3).toInt(&ok);
    if (!ok | (n < 0) | (n > 255)) return false;
    return true;
}

QString ModbusSnifferPlugin::ip2Hex(const QString &ip)
{
    bool ok;
    int p1 = ip.indexOf(".");		// get first point position
    if (p1 == -1) return "";
    int p2 = ip.indexOf(".", p1 + 1);	// get second point position
    if (p2 == -1) return "";
    int p3 = ip.indexOf(".", p2 + 1);	// get third point position
    if (p3 == -1) return "";
    int l = ip.length();
    QString N1 = ip.mid(0, p1);
    if (N1 == "") return "";
    QString N2 = ip.mid(p1 + 1, p2 - p1 - 1);
    if (N2 == "") return "";
    QString N3 = ip.mid(p2 + 1, p3 - p2 - 1);
    if (N3 == "") return "";
    QString N4 = ip.mid(p3 + 1, l - p3 - 1);
    if (N4 == "") return "";
    int n1 = N1.toInt(&ok);
    if (!ok) return "";
    int n2 = N2.toInt(&ok);
    if (!ok) return "";
    int n3 = N3.toInt(&ok);
    if (!ok) return "";
    int n4 = N4.toInt(&ok);
    if (!ok) return "";
    return QString("%1%2%3%4").arg(n1, 2, 16, QLatin1Char('0')).arg(n2, 2, 16, QLatin1Char('0')).arg(n3, 2, 16, QLatin1Char('0')).arg(n4, 2, 16, QLatin1Char('0')).toUpper();
}


void ModbusSnifferPlugin::log(const QString str)
{
    if (mui->checkBoxLog->isChecked())
    {
        mui->logTxt->append(QDateTime::currentDateTime().toString("HH:mm:ss ") + str);
        QString currenttext = mui->logTxt->toPlainText();
        if (currenttext.length() > 10000) mui->logTxt->setText(currenttext.mid(9000));
        mui->logTxt->moveCursor(QTextCursor::End);
    }
}

void ModbusSnifferPlugin::logWr(const QString str)
{
    if ( mui->checkBoxLog->isChecked())
    {
        mui->logTxt->append(QDateTime::currentDateTime().toString("HH:mm:ss ") + str);
        QString currenttext = mui->logTxt->toPlainText();
        if (currenttext.length() > 10000) mui->logTxt->setText(currenttext.mid(9000));
        mui->logTxt->moveCursor(QTextCursor::End);
    }
}


void ModbusSnifferPlugin::showLog()
{
}


void ModbusSnifferPlugin::on_AddRegisterButton_clicked()
{
    bool ok;
    foreach (QModelIndex index, mui->m_table->selectionModel()->selectedRows()) {    // get each selected line
        const int row = index.row();
        const QString slave = mui->m_table->item(row, 0)->text();     // Esclave
        const QString address = mui->m_table->item(row, 1)->text();   // Adresse
        const QString type = mui->m_table->item(row, 2)->text();      // Type (holding / input)
        // check if register is already used
        bool found = false;
        foreach (modbusItem *dev, modbusDevices) {
            if ((dev->slave->text().toInt(&ok) == slave.toInt()) && (dev->address->text().toInt(&ok) == address.toInt())
                && (dev->type->currentText() == type)) found = true;
        }
        if (found) continue;
        // get next RomID
        int count = 1;
        bool idfound = true;
        while (idfound) {
            idfound = false;
            QString id = buildRomID(count);
            foreach (modbusItem *dev, modbusDevices) {
                if (dev->RomID == id) idfound = true;  }
            if (idfound) count++;
        }
        modbusItem *dev = addDevice(buildRomID(count));
        if (dev) {
            dev->slave->setText(slave);
            dev->address->setText(address);
            dev->type->setCurrentText(type);
        }
    }
}


void ModbusSnifferPlugin::on_AddButton_clicked()
{
    if (!isIpValid(m_ip)) {
        QMessageBox msgBox;
        msgBox.setText("IP address is not valid");
        msgBox.exec();
        return;
    }
    if (!isPortValid(mui->editPort->text())) {
        QMessageBox msgBox;
        msgBox.setText("Port is not valid");
        msgBox.exec();
        return;
    }
    int count = 1;
    bool found = true;
    while (found) {
        found = false;
        QString id = buildRomID(count);
        foreach (modbusItem *dev, modbusDevices) {
            if (dev->RomID == id) found = true;  }
        if (found) count++;
    }
    addDevice(buildRomID(count));
}

modbusItem *ModbusSnifferPlugin::addDevice(QString id)
{
// Get table size
    bool ok;
    int count = id.right(5).left(3).toInt(&ok) - 1; // a faire ici, decompte pas juste
    const int last = qMin(mui->deviceTable->rowCount(), int(modbusDevices.size()));
    if (!ok || count < 0 || count > last) count = last;   // ID vide ou mal formé : ajout en fin de table
// add one line
    mui->deviceTable->insertRow(count);
// Generate RomID
    if (id.isEmpty()) id = buildRomID(count + 1);
// add device in the QHash store
    modbusItem *newDev = new modbusItem(id);
    if (!newDev) return nullptr;
    modbusDevices.insert(count, newDev);
// add RomID widget in the table
    QTableWidgetItem *item = new QTableWidgetItem(id);
    item->setFlags(item->flags() ^ Qt::ItemIsEditable);  // set RomID widget not editable
    mui->deviceTable->setItem(count, 0, item);
    // add Slave input item
    mui->deviceTable->setItem(count, 1, newDev->slave);
// add Address input item
    mui->deviceTable->setItem(count, 2, newDev->address);
    //newDev->address->setText(QString("%1").arg(count +1));
// add Type combo input item
    mui->deviceTable->setCellWidget(count, 3, newDev->type);
// add Typedef combo input item
    mui->deviceTable->setCellWidget(count, 4, newDev->typeDef);
// add Read Interval combo input item
    //mui->deviceTable->setCellWidget(count, 5, newDev->readInterval);
// set Value item
    mui->deviceTable->setItem(count, 5, newDev->value);
// set Last Read item
    mui->deviceTable->setItem(count, 6, newDev->lastRead);
    mui->deviceTable->setItem(count, 7, newDev->maxValue);
    emit(newDevice(this, id));
    return newDev;
}



void ModbusSnifferPlugin::updateIpRomID()
{
    int count = mui->deviceTable->rowCount();
    for (int n=0; n<count; n++) {  // tableWidget
        QString RomID = buildRomID(n+1);
        mui->deviceTable->item(n, 0)->setText(RomID);
        foreach (modbusItem *dev, modbusDevices) {  // modbusDevice
            if (dev->id == n+1) dev->RomID = RomID; }
    }
}



void ModbusSnifferPlugin::on_RemoveButton_clicked()
{
    QModelIndexList indexList = mui->deviceTable->selectionModel()->selectedIndexes();
    if (indexList.isEmpty()) return;    // rien de sélectionné (ou table vide)
    int row = indexList.last().row();
    QTableWidgetItem *romIdItem = mui->deviceTable->item(row, 0);
    if (!romIdItem) return;
    foreach (modbusItem *dev, modbusDevices) {
        if (dev->RomID == romIdItem->text()) {
            //qDebug() << QString("Remove ") + dev->RomID;
            modbusDevices.removeAll(dev);
            mui->deviceTable->removeRow(row);
            mui->RemoveButton->setEnabled(false);
            return;
        }
    }
}




int ModbusSnifferPlugin::typeToSize(int type)
{
    switch (type) {
    case uint16b :
    case int16b : return 1;
    case uint32bMSDFirst :
    case uint32bMSDLast : return 2;
    }
    return 1;
}


void ModbusSnifferPlugin::sortDevicesList()
{
    int n;
    int i;
    for (n=0; n < modbusDevices.count(); n++)
    {
        for (i=n+1; i < modbusDevices.count(); i++)
        {
            bool ok;
            int32_t valorN = modbusDevices.at(n)->slave->text().toInt(&ok) << 16;
            int32_t valorI = modbusDevices.at(i)->slave->text().toInt(&ok) << 16;
            valorN += modbusDevices.at(n)->address->text().toInt(&ok);
            valorI += modbusDevices.at(i)->address->text().toInt(&ok);
            if (valorN > valorI) {
                modbusDevices.move(i, n);
                n=0;}
        }
    }
    foreach (modbusItem *item, modbusDevices) { item->address->setBackground(QColor(Qt::white)); }
    for (n=0; n < modbusDevices.count(); n++)
    {
        for (i=n+1; i < modbusDevices.count(); i++)
        {
            bool ok;
            int32_t valorN = modbusDevices.at(n)->slave->text().toInt(&ok) << 16;
            int32_t valorI = modbusDevices.at(i)->slave->text().toInt(&ok) << 16;
            valorN += modbusDevices.at(n)->address->text().toInt(&ok);
            valorI += modbusDevices.at(i)->address->text().toInt(&ok);
            if (valorN == valorI) {
                modbusDevices.at(i)->address->setBackground(QColor(Qt::gray));
                modbusDevices.at(i-1)->address->setBackground(QColor(Qt::gray));
                }
        }
    }
}


void ModbusSnifferPlugin::sortDevicesId()
{
    int n;
    int i;
    for (n=0; n < modbusDevices.count(); n++)
    {
        for (i=n+1; i < modbusDevices.count(); i++)
        {
            int16_t valorN = modbusDevices.at(n)->id;
            int16_t valorI = modbusDevices.at(i)->id;
            if (valorN > valorI) {
                modbusDevices.move(i, n);
                n=0;}
        }
    }
}



void ModbusSnifferPlugin::on_ClearLogButton_clicked()
{
    mui->logTxt->clear();
}


void ModbusSnifferPlugin::checkCombo()
{
    sortDevicesList();
}


void ModbusSnifferPlugin::displayDevice(int row, int column)
{
    if (column == 0)
    {
        QString RomID = mui->deviceTable->item(row, column)->text();
        emit(deviceSelected(RomID));
    }
}


void ModbusSnifferPlugin::editIP_editingFinished()
{
    if (!isIpValid(mui->editIP->text())) {
        QMessageBox msgBox;
        msgBox.setText("IP address is not valid");
        msgBox.exec();
        return;
    }
    if (mui->editIP->text() == m_ip) return;
    m_ip = mui->editIP->text();
    updateIpRomID();
    if (m_thread) { stopWorker(); startWorker(); }   // prise en compte de la nouvelle adresse
}


void ModbusSnifferPlugin::on_editPort_editingFinished()
{
    if (!isPortValid(mui->editPort->text())) {
        QMessageBox msgBox;
        msgBox.setText("Port is not valid");
        msgBox.exec();
        return;
    }
    const quint16 port = quint16(mui->editPort->text().toInt());
    if (port == m_port) return;
    m_port = port;
    updateIpRomID();
    if (m_thread) { stopWorker(); startWorker(); }   // prise en compte du nouveau port
}



void ModbusSnifferPlugin::on_editName_editingFinished()
{
    emit(updateInterfaceName(this, mui->editName->text()));
}



