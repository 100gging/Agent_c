#include "networkmanager.h"
#include <QDebug>

// =====================================================================
// 생성자: 역할에 따라 서버/클라이언트 멤버 초기화
// =====================================================================
NetworkManager::NetworkManager(Role role, QObject *parent)
    : QObject(parent),
      m_role(role),
      m_server(nullptr),
      m_clientSocket(nullptr),
      m_peerReady(false),
      m_localReady(false),
      m_socket(nullptr),
      m_retryTimer(nullptr),
      m_retryCount(0),
      m_clientReady(false),
      m_modeLocked(false)
{
    if (m_role == Server) {
        // 서버 모드: QTcpServer 생성
        m_server = new QTcpServer(this);

        // 새 클라이언트가 접속하면 onNewConnection 슬롯 호출
        connect(m_server, &QTcpServer::newConnection,
                this,     &NetworkManager::onNewConnection);

        qDebug() << "[NetworkManager] 서버 모드로 초기화됨. 포트:" << SERVER_PORT;

    } else {
        // 클라이언트 모드: QTcpSocket 생성
        m_socket = new QTcpSocket(this);

        // 연결 성공 시
        connect(m_socket, &QTcpSocket::connected,
                this,     &NetworkManager::onConnected);

        // 데이터 수신 시
        connect(m_socket, &QTcpSocket::readyRead,
                this,     &NetworkManager::onReadyRead);

        // 연결 끊김 시
        connect(m_socket, &QTcpSocket::disconnected,
                this,     &NetworkManager::onDisconnected);

        // 소켓 에러 발생 시 (연결 실패 포함)
        connect(m_socket, &QAbstractSocket::errorOccurred,
                this, &NetworkManager::onSocketError);

        // 재연결 타이머: 2초 후 1회 재시도
        m_retryTimer = new QTimer(this);
        m_retryTimer->setSingleShot(true); // 한 번만 발동
        connect(m_retryTimer, &QTimer::timeout,
                this,          &NetworkManager::onRetryConnect);

        qDebug() << "[NetworkManager] 클라이언트 모드로 초기화됨. 서버:"
                 << SERVER_IP << ":" << SERVER_PORT;
    }
}

// =====================================================================
// 소멸자
// =====================================================================
NetworkManager::~NetworkManager()
{
    // QObject 부모 계층으로 자동 해제됨
}

// =====================================================================
// start(): 네트워크 동작 시작
// =====================================================================
void NetworkManager::start()
{
    if (m_role == Server) {
        // 서버: 모든 IP에서 SERVER_PORT 로 listen 시작
        // 먼저 임시로 listen하여 native fd를 얻고, SO_REUSEADDR 설정
        if (!m_server->listen(QHostAddress::Any, SERVER_PORT)) {
            qDebug() << "[Server] listen 실패:" << m_server->errorString()
                     << "— 포트가 TIME_WAIT 상태일 수 있습니다.";
            return;
        }
        qDebug() << "[Server] 포트" << SERVER_PORT << "에서 대기 중...";

    } else {
        // 클라이언트: 서버 IP로 connect 시도
        qDebug() << "[Client] 서버에 접속 시도 →"
                 << SERVER_IP << ":" << SERVER_PORT;
        m_socket->connectToHost(QString(SERVER_IP), SERVER_PORT);
    }
}

// =====================================================================
// sendReady(): 로컬 플레이어가 버튼을 눌렀을 때 호출
// =====================================================================
void NetworkManager::sendReady()
{
    if (m_role == Server) {
        // 서버 측: 이미 READY 전송했으면 무시 (중복 방지)
        if (m_localReady) {
            qDebug() << "[Server] 이미 READY 상태 — 무시";
            return;
        }
        m_localReady = true;

        if (m_clientSocket && m_clientSocket->state() == QAbstractSocket::ConnectedState) {
            // 연결된 클라이언트에게 READY 전송
            m_clientSocket->write("READY\n");
            m_clientSocket->flush();
            qDebug() << "[Server] 클라이언트에게 READY 전송";

            // 이미 클라이언트도 READY 상태라면 GO 전송
            checkAndSendGo();
        } else {
            // 연결 없음 → 피어 접속을 계속 대기 (즉시 시작하지 않음)
            qDebug() << "[Server] 피어 없음 — 접속 대기 중 (READY 상태 유지)";
        }

    } else {
        // 클라이언트 측: 이미 READY 전송했으면 무시
        if (m_clientReady) {
            qDebug() << "[Client] 이미 READY 상태 — 무시";
            return;
        }
        m_clientReady = true;

        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            // 서버에게 READY 전송
            m_socket->write("READY\n");
            m_socket->flush();
            qDebug() << "[Client] 서버에게 READY 전송";
        } else {
            // 연결 없음 → 피어 접속을 계속 대기 (즉시 시작하지 않음)
            qDebug() << "[Client] 서버 미연결 — 접속 대기 중 (READY 상태 유지)";
        }
    }
}

// =====================================================================
// isConnected(): 현재 소켓 연결 상태 반환
// =====================================================================
bool NetworkManager::isConnected() const
{
    if (m_role == Server)
        return (m_clientSocket &&
                m_clientSocket->state() == QAbstractSocket::ConnectedState);
    else
        return (m_socket &&
                m_socket->state() == QAbstractSocket::ConnectedState);
}

// =====================================================================
// resetReady(): READY 플래그 초기화 (게임 종료 후 재시작 시 호출)
// =====================================================================
void NetworkManager::resetReady()
{
    m_localReady  = false;
    m_peerReady   = false;
    m_clientReady = false;
    qDebug() << "[NetworkManager] READY 플래그 초기화됨";
}

// =====================================================================
// [서버 전용] 새 클라이언트 접속 처리
// =====================================================================
void NetworkManager::onNewConnection()
{
    // nextPendingConnection()으로 접속한 소켓을 가져옴
    QTcpSocket *socket = m_server->nextPendingConnection();
    if (!socket) return;

    // 기존 연결이 있으면 해제 (1:1 연결만 허용)
    if (m_clientSocket) {
        qDebug() << "[Server] 기존 연결 대체됨";
        m_clientSocket->disconnectFromHost();
        m_clientSocket->deleteLater();
    }

    m_clientSocket = socket;
    m_clientSocket->setParent(this);
    m_clientSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1); // TCP_NODELAY

    // 클라이언트로부터 데이터 수신 시 슬롯 연결
    connect(m_clientSocket, &QTcpSocket::readyRead,
            this,            &NetworkManager::onReadyRead);

    // 클라이언트 연결 끊김 시 슬롯 연결
    connect(m_clientSocket, &QTcpSocket::disconnected,
            this,            &NetworkManager::onDisconnected);

    qDebug() << "[Server] 클라이언트 접속:"
             << m_clientSocket->peerAddress().toString();

    // 서버가 이미 READY를 눌렀었다면, 새로 접속한 클라이언트에게 READY 전송
    if (m_localReady) {
        m_clientSocket->write("READY\n");
        m_clientSocket->flush();
        qDebug() << "[Server] 이미 READY 상태 — 클라이언트에게 READY 전송";
    }
}

// =====================================================================
// onReadyRead(): 소켓에 읽을 데이터가 있을 때 호출 (서버/클라이언트 공통)
// =====================================================================
void NetworkManager::onReadyRead()
{
    // 어느 소켓에서 시그널이 왔는지 확인
    QTcpSocket *senderSocket = qobject_cast<QTcpSocket*>(sender());
    if (!senderSocket) return;

    if (m_role == Server) {
        // 서버: 클라이언트로부터 받은 메시지 처리
        processMessages(senderSocket, true);
    } else {
        // 클라이언트: 서버로부터 받은 메시지 처리
        processMessages(senderSocket, false);
    }
}

// =====================================================================
// processMessages(): 수신 버퍼를 줄 단위로 파싱하여 처리
// =====================================================================
void NetworkManager::processMessages(QTcpSocket *socket, bool isFromClient)
{
    while (socket->canReadLine()) {
        QString msg = QString::fromUtf8(socket->readLine()).trimmed();

        // 빈번한 게임플레이 메시지는 로그 생략
        if (msg != "FIRE" && !msg.startsWith("AIM ") && !msg.startsWith("STATE "))
            qDebug() << (isFromClient ? "[Server] 수신:" : "[Client] 수신:") << msg;

        if (msg == "READY") {
            if (isFromClient) {
                m_peerReady = true;
                emit peerReady();
                qDebug() << "[Server] 피어(클라이언트) READY 확인";
                checkAndSendGo();
            } else {
                emit peerReady();
                qDebug() << "[Client] 피어(서버) READY 확인";
            }

        } else if (msg == "GO") {
            qDebug() << (isFromClient ? "[Server]" : "[Client]") << "GO 수신 → 게임 시작";
            emit syncGo();

        } else if (msg.startsWith("AIM ") && isFromClient) {
            // 서버: 클라이언트 에임 수신
            QStringList parts = msg.split(' ');
            if (parts.size() >= 3)
                emit aimReceived(parts[1].toInt(), parts[2].toInt());

        } else if (msg == "FIRE" && isFromClient) {
            // 서버: 클라이언트 발사 수신
            emit fireReceived();

        } else if (msg.startsWith("STATE ") && !isFromClient) {
            // 클라이언트: 서버 게임 상태 수신
            emit stateReceived(msg.mid(6));

        } else if ((msg == "MODE_COOP" || msg == "MODE_COMP") && isFromClient) {
            // 서버: 클라이언트가 모드 선택 → 선착순 확정 후 에코
            if (!m_modeLocked) {
                m_modeLocked = true;
                bool coop = (msg == "MODE_COOP");
                const char *reply = coop ? "MODE_COOP\n" : "MODE_COMP\n";
                if (m_clientSocket && m_clientSocket->state() == QAbstractSocket::ConnectedState) {
                    m_clientSocket->write(reply);
                    m_clientSocket->flush();
                }
                emit modeReceived(coop);
            }

        } else if ((msg == "MODE_COOP" || msg == "MODE_COMP") && !isFromClient) {
            // 클라이언트: 서버로부터 확정된 모드 수신
            emit modeReceived(msg == "MODE_COOP");

        } else if (msg.startsWith("SAIM ") && !isFromClient) {
            // 클라이언트: ModeSelect 중 서버 에임 수신
            QStringList parts = msg.split(' ');
            if (parts.size() >= 3)
                emit aimReceived(parts[1].toInt(), parts[2].toInt());
        }
    }
}

// =====================================================================
// [서버 전용] 양쪽 READY 확인 후 GO 전송
// =====================================================================
void NetworkManager::checkAndSendGo()
{
    // 서버 로컬 + 피어(클라이언트) 모두 READY 인 경우에만 GO 전송
    if (!m_localReady || !m_peerReady) return;

    qDebug() << "[Server] 양쪽 READY 확인 → GO 전송";

    // 클라이언트에게 GO 전송
    if (m_clientSocket &&
        m_clientSocket->state() == QAbstractSocket::ConnectedState) {
        m_clientSocket->write("GO\n");
        m_clientSocket->flush();
    }

    // 서버 자신도 게임 시작 (syncGo 시그널)
    emit syncGo();
}

// =====================================================================
// onDisconnected(): 소켓 연결 끊김 처리
// =====================================================================
void NetworkManager::onDisconnected()
{
    qDebug() << "[NetworkManager] 연결 끊김";

    if (m_role == Server) {
        // 서버: 클라이언트 소켓 초기화
        if (m_clientSocket) {
            m_clientSocket->deleteLater();
            m_clientSocket = nullptr;
        }
        // READY 상태 초기화 (재접속 대비)
        m_peerReady  = false;
        m_localReady = false;
    }

    // MainWindow에게 연결 끊김 알림
    emit connectionLost();
}

// =====================================================================
// [클라이언트 전용] 서버 연결 성공 시 처리
// =====================================================================
void NetworkManager::onConnected()
{
    qDebug() << "[Client] 서버 연결 성공:"
             << SERVER_IP << ":" << SERVER_PORT;

    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1); // TCP_NODELAY

    // 재연결 타이머가 동작 중이라면 취소
    if (m_retryTimer && m_retryTimer->isActive())
        m_retryTimer->stop();

    // 연결 성공: 재시도 카운터 및 READY 플래그 초기화
    m_retryCount = 0;
    m_clientReady = false;
}

// =====================================================================
// [클라이언트 전용] 소켓 에러 처리 (연결 실패 포함)
// =====================================================================
void NetworkManager::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    qDebug() << "[Client] 소켓 에러:" << m_socket->errorString();

    if (m_retryCount < MAX_RETRIES) {
        m_retryCount++;
        qDebug() << QString("[Client] %1초 후 재연결 시도... (%2/%3)")
                    .arg(2).arg(m_retryCount).arg(MAX_RETRIES);
        m_retryTimer->start(2000);
    } else {
        qDebug() << "[Client] 최대 재시도 초과 — connectionLost 시그널 emit";
        emit connectionLost();
    }
}

// =====================================================================
// [클라이언트 전용] 재연결 타이머 발동 시 처리
// =====================================================================
void NetworkManager::onRetryConnect()
{
    qDebug() << "[Client] 재연결 시도 →" << SERVER_IP << ":" << SERVER_PORT;

    // 소켓 상태를 Unconnected로 초기화 후 재접속
    m_socket->abort();
    m_socket->connectToHost(QString(SERVER_IP), SERVER_PORT);
}

// =====================================================================
// sendAim(): 클라이언트 → 서버: 에임 좌표 전송
// =====================================================================
void NetworkManager::sendAim(int x, int y)
{
    if (m_role != Client) return;
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write(QString("AIM %1 %2\n").arg(x).arg(y).toUtf8());
}

// =====================================================================
// sendFire(): 클라이언트 → 서버: 발사 이벤트 전송
// =====================================================================
void NetworkManager::sendFire()
{
    if (m_role != Client) return;
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write("FIRE\n");
        m_socket->flush();
    }
}

// =====================================================================
// sendState(): 서버 → 클라이언트: 게임 상태 전송
// =====================================================================
void NetworkManager::sendState(const QString &stateData)
{
    if (m_role != Server) return;
    if (m_clientSocket && m_clientSocket->state() == QAbstractSocket::ConnectedState)
        m_clientSocket->write(("STATE " + stateData + "\n").toUtf8());
}

// =====================================================================
// sendMode(): 모드 선택 전송
// =====================================================================
void NetworkManager::sendMode(bool cooperative)
{
    const QByteArray msg = cooperative ? "MODE_COOP\n" : "MODE_COMP\n";
    if (m_role == Server) {
        // 서버가 먼저 선택한 경우: 직접 잠금 후 클라이언트에 전파
        if (m_modeLocked) return;
        m_modeLocked = true;
        if (m_clientSocket && m_clientSocket->state() == QAbstractSocket::ConnectedState) {
            m_clientSocket->write(msg);
            m_clientSocket->flush();
        }
        emit modeReceived(cooperative);
    } else {
        // 클라이언트: 서버에 요청 전송 후 서버 확인 대기
        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->write(msg);
            m_socket->flush();
        }
    }
}

// =====================================================================
// sendServerAim(): 서버 → 클라이언트: ModeSelect 화면에서 서버 에임 전송
// =====================================================================
void NetworkManager::sendServerAim(int x, int y)
{
    if (m_role != Server) return;
    if (m_clientSocket && m_clientSocket->state() == QAbstractSocket::ConnectedState)
        m_clientSocket->write(QString("SAIM %1 %2\n").arg(x).arg(y).toUtf8());
}

// =====================================================================
// resetModeLock(): 모드 잠금 초기화 (retry 시 호출)
// =====================================================================
void NetworkManager::resetModeLock()
{
    m_modeLocked = false;
    qDebug() << "[NetworkManager] 모드 잠금 초기화됨";
}
