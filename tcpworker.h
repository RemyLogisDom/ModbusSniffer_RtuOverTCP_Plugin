#ifndef TCPWORKER_H
#define TCPWORKER_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QByteArray>
#include <QString>
#include <vector>
#include <cstdint>

#include "modbus_rtu_core.hpp"

// Worker TCP : vit dans son propre QThread (moveToThread) et tourne sur la
// boucle d'événements de ce thread. Aucune attente bloquante : la reconnexion
// est cadencée par un timer, donc pas de boucle qui tourne à vide quand le
// convertisseur refuse la connexion.
class TcpWorker : public QObject
{
    Q_OBJECT

public:
    explicit TcpWorker(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    // A appeler AVANT moveToThread.
    void setEndpoint(const QString &ip, quint16 port)
    {
        m_ip = ip;
        m_port = port;
    }

public slots:
    // A connecter à QThread::started
    void start()
    {
        m_running = true;

        // Créés ici => ils vivent dans le thread du worker.
        m_socket = new QTcpSocket(this);
        m_retryTimer = new QTimer(this);
        m_retryTimer->setInterval(RetryIntervalMs);

        connect(m_socket, &QAbstractSocket::stateChanged, this,
                [this](QAbstractSocket::SocketState state) {
                    emit tcpStatusChange(int(state));
                });

        connect(m_socket, &QAbstractSocket::connected, this, [this]() {
            m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
            m_lastData.start();
            emit logTCP("Connected to " + m_ip + QString(":%1").arg(m_port));
        });

        connect(m_socket, &QAbstractSocket::disconnected, this, [this]() {
            emit logTCP("Disconnected");
        });

        connect(m_socket, &QAbstractSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) {
                    emit logTCP("Socket error : " + m_socket->errorString());
                });

        connect(m_socket, &QIODevice::readyRead, this, [this]() {
            m_lastData.start();
            const QByteArray data = m_socket->readAll();
            emit logTCP("get : " + data.toHex(' ').toUpper());
            traiterOctets(data);
        });

        // Watchdog / reconnexion, à chaque tick :
        //  - déconnecté => on retente
        //  - bloqué en "connecting" => on abandonne, le tick suivant retentera
        //  - connecté mais muet depuis DataTimeoutMs => on coupe pour reconnecter
        connect(m_retryTimer, &QTimer::timeout, this, [this]() { tryConnect(); });

        emit logTCP("Modbus start");
        tryConnect();
        m_retryTimer->start();
    }

    // A appeler via QMetaObject::invokeMethod depuis un autre thread.
    void stop()
    {
        m_running = false;
        if (m_retryTimer)
            m_retryTimer->stop();
        if (m_socket)
            m_socket->abort(); // fermeture immédiate, sans attente
        emit logTCP("finished");
    }

    void traiterOctets(const QByteArray &octets)
    {
        if (octets.isEmpty())
            return;

        // QByteArray -> std::vector<uint8_t> : on passe par des pointeurs
        // unsigned explicites pour éviter tout souci de signe sur `char`.
        const auto *debut = reinterpret_cast<const uint8_t *>(octets.constData());
        modbus_rtu::Octets tampon(debut, debut + octets.size());

        // Le flux n'a plus de timing fiable une fois passé par le réseau :
        // on horodate au moment où on traite le paquet.
        const double horodatage = QDateTime::currentMSecsSinceEpoch() / 1000.0;

        const std::vector<modbus_rtu::TrameDecodee> trames =
            m_suivi.alimenter_flux(tampon, horodatage);

        for (const modbus_rtu::TrameDecodee &trame : trames) {
            emit trameDecodee(trame.adresse_esclave, trame.code_fonction,
                              QString::fromStdString(trame.type_trame), trame.est_exception);

            if (!trame.crc_valide) {
                emit trameCrcInvalide(QByteArray(reinterpret_cast<const char *>(trame.brute.data()),
                                                 static_cast<int>(trame.brute.size())));
            }
        }

        // Mises à jour de registre déclenchées par ce paquet (la file est
        // vidée par l'appel, rien ne s'accumule d'un paquet à l'autre).
        for (const modbus_rtu::EvenementRegistre &e : m_suivi.extraire_evenements()) {
            emit registreMisAJour(e.esclave, QString::fromStdString(e.type_zone), e.adresse,
                                  e.valeur, QString::fromStdString(e.source));
        }
    }

signals:
    void tcpStatusChange(int);
    void logTCP(QString);

    // Émis pour CHAQUE trame valide décodée (requête ou réponse).
    void trameDecodee(quint8 esclaveAdresse, quint8 codeFonction, const QString &typeTrame, bool estException);

    // Émis quand une trame candidate échoue au contrôle CRC.
    void trameCrcInvalide(const QByteArray &trameBrute);

    // Émis à chaque mise à jour d'une valeur de registre/bobine.
    void registreMisAJour(quint8 esclaveAdresse, const QString &zone, quint16 adresse,
                          quint16 valeur, const QString &source);

private:
    static constexpr int RetryIntervalMs = 3000;
    static constexpr qint64 DataTimeoutMs = 60000;

    void tryConnect()
    {
        if (!m_running || !m_socket)
            return;

        switch (m_socket->state()) {
        case QAbstractSocket::UnconnectedState:
            emit logTCP("Try to connect to " + m_ip + QString(":%1").arg(m_port));
            m_socket->connectToHost(m_ip, m_port);
            break;
        case QAbstractSocket::ConnectingState:
        case QAbstractSocket::HostLookupState:
            // Toujours en cours au tick suivant => on abandonne.
            m_socket->abort();
            break;
        case QAbstractSocket::ConnectedState:
            if (m_lastData.isValid() && m_lastData.elapsed() > DataTimeoutMs) {
                emit logTCP(QString("No data for %1 s, reconnecting").arg(DataTimeoutMs / 1000));
                m_socket->abort();
            }
            break;
        default:
            break;
        }
    }

    QString m_ip;
    quint16 m_port = 1470;
    bool m_running = false;

    QTcpSocket *m_socket = nullptr;
    QTimer *m_retryTimer = nullptr;
    QElapsedTimer m_lastData;

    modbus_rtu::SuiviModbus m_suivi;
};

#endif // TCPWORKER_H
