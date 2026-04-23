#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

/**
 * @brief NetworkManager
 *
 * 두 라즈베리파이 간의 TCP 동기화를 담당하는 클래스.
 *
 * [동작 흐름]
 * - 서버 모드 (--server, IP: 192.168.10.4):
 *     listen(5000) → 클라이언트 접속 대기
 *     양쪽이 모두 "READY\n" 를 보내면 → "GO\n" 를 양쪽에 전송 → syncGo() 시그널
 *
 * - 클라이언트 모드 (--client, IP: 192.168.10.3):
 *     connectToHost("192.168.10.4", 5000)
 *     서버로부터 "GO\n" 수신 시 → syncGo() 시그널
 *
 * [프로토콜]
 *   READY\n  : 해당 플레이어가 버튼을 눌렀음을 알림
 *   GO\n     : 서버가 양쪽 준비 확인 후 동시 시작 신호 전송
 */
class NetworkManager : public QObject
{
    Q_OBJECT

public:
    // 서버/클라이언트 역할 구분
    enum Role { Server, Client };

    // 서버 IP 및 포트 (요구사항에 따라 고정)
    static constexpr quint16 SERVER_PORT = 5000;
    static constexpr const char* SERVER_IP = "192.168.10.4";

    explicit NetworkManager(Role role, QObject *parent = nullptr);
    ~NetworkManager();

    /**
     * @brief 네트워크 시작
     *   - 서버: listen 시작
     *   - 클라이언트: 서버에 connect 시도
     */
    void start();

    /**
     * @brief 로컬 플레이어가 READY 버튼을 눌렀을 때 호출.
     *        피어에게 "READY\n" 메시지를 전송하고 로컬 ready 플래그를 설정.
     *        네트워크가 연결되지 않은 경우에는 즉시 syncGo() 시그널을 emit (단독 모드 fallback).
     */
    void sendReady();

    /** @brief READY 플래그 초기화 (게임 종료 후 재시작 시 호출) */
    void resetReady();

    /** @brief 현재 피어와 연결된 상태인지 반환 */
    bool isConnected() const;

    /** @brief 서버/클라이언트 역할 반환 */
    Role role() const { return m_role; }

    // ── 게임플레이 메시지 전송 (Playing 상태) ──
    /** 클라이언트 → 서버: 매 프레임 에임 좌표 전송 */
    void sendAim(int x, int y);
    /** 클라이언트 → 서버: 발사 이벤트 전송 */
    void sendFire();
    /** 서버 → 클라이언트: 게임 전체 상태 전송 */
    void sendState(const QString &stateData);

    // ── 모드 선택 메시지 전송 ──
    /** 모드 선택 확정 전송 (서버: 클라이언트에게 전파, 클라이언트: 서버에게 요청) */
    void sendMode(bool cooperative);
    /** 서버 → 클라이언트: ModeSelect 화면에서 서버 에임 좌표 전송 */
    void sendServerAim(int x, int y);
    /** 모드 잠금 초기화 (retry 시 호출) */
    void resetModeLock();

    // ── 사진/랭킹 전송 ──
    /** 상대에게 촬영한 사진 파일 전송 (base64) — 서버/클라이언트 양방향 */
    void sendPhoto(const QString &filePath);
    /** 서버 → 클라이언트: 협동 랭킹 데이터 전송 */
    void sendCoopRanking(const QString &jsonData);
    /** 메뉴 복귀 시 BGM 동기화 요청 전송 */
    void sendMenuBgm();

    /** 클라이언트 → 서버: 일시정지 요청 (클라이언트가 Settings 진입 시) */
    void sendPause();
    /** 클라이언트 → 서버: 재개 요청 (클라이언트가 Settings 복귀 시) */
    void sendResume();

signals:
    /**
     * @brief 상대방이 READY를 전송했을 때 emit.
     *        UI에서 "Waiting for opponent..." 표시 해제 등에 활용 가능.
     */
    void peerReady();

    /**
     * @brief 양쪽 모두 READY → 서버가 GO 를 보냄 → 수신 측에서 emit.
     *        MainWindow 는 이 시그널을 받아 startGame() 을 호출한다.
     */
    void syncGo();

    /**
     * @brief 연결이 끊기거나 재연결에 실패했을 때 emit.
     *        MainWindow 는 이 시그널을 받아 Menu 상태로 복귀한다.
     */
    void connectionLost();

    // ── 게임플레이 수신 시그널 ──
    /** 서버가 클라이언트 에임 수신 시 emit */
    void aimReceived(int x, int y);
    /** 서버가 클라이언트 FIRE 수신 시 emit */
    void fireReceived();
    /** 클라이언트가 서버 STATE 수신 시 emit */
    void stateReceived(const QString &stateData);
    /** 모드 선택 메시지 수신 시 emit */
    void modeReceived(bool cooperative);
    /** 서버가 클라이언트 사진 수신 시 emit (base64 디코딩된 JPEG 데이터) */
    void photoReceived(const QByteArray &jpegData);
    /** 클라이언트가 협동 랭킹 데이터 수신 시 emit */
    void coopRankingReceived(const QString &jsonData);
    /** BGM 동기 재생 시그널 (양쪽 동시) */
    void syncBgm();
    /** 서버: 클라이언트가 PAUSE 요청 시 emit */
    void pauseReceived();
    /** 서버: 클라이언트가 RESUME 요청 시 emit */
    void resumeReceived();

private slots:
    // [서버 전용] 새 클라이언트 접속 처리
    void onNewConnection();

    // 소켓으로부터 데이터 수신 처리 (서버/클라이언트 공통)
    void onReadyRead();

    // 소켓 연결 끊김 처리
    void onDisconnected();

    // [클라이언트 전용] connect 성공 시
    void onConnected();

    // [클라이언트 전용] connect 에러 처리
    void onSocketError(QAbstractSocket::SocketError error);

    // [클라이언트 전용] 재연결 타이머 슬롯
    void onRetryConnect();

private:
    /**
     * @brief 수신된 데이터를 한 줄씩 파싱하여 처리.
     * @param socket 데이터를 읽을 소켓
     * @param isFromClient true이면 서버 측에서 클라이언트 메시지 처리
     */
    void processMessages(QTcpSocket *socket, bool isFromClient);

    /**
     * @brief 서버 측: 양쪽 모두 READY 상태인지 확인하고, GO 신호 전송.
     */
    void checkAndSendGo();

    Role        m_role;             // 서버 또는 클라이언트 역할

    // ----- 서버 전용 멤버 -----
    QTcpServer *m_server;           // TCP 서버 리스너
    QTcpSocket *m_clientSocket;     // 서버가 수락한 클라이언트 소켓
    bool        m_peerReady;        // 피어(클라이언트)의 READY 수신 여부
    bool        m_localReady;       // 로컬(서버) READY 전송 여부

    // ----- 클라이언트 전용 멤버 -----
    QTcpSocket *m_socket;           // 클라이언트 소켓
    QTimer     *m_retryTimer;       // 재연결 타이머
    int         m_retryCount;       // 재연결 시도 횟수
    static constexpr int MAX_RETRIES = 15;  // 최대 재시도 (15회 × 2초 = 30초)
    bool        m_clientReady;      // 클라이언트 로컬 READY 전송 여부
    bool        m_modeLocked;       // 모드 선착순 잠금 (서버 전용)
    bool        m_menuBgmLocal;     // 서버 자신이 메뉴 복귀 BGM 대기 중
    bool        m_menuBgmPeer;      // 클라이언트가 메뉴 복귀 BGM 대기 중
};

#endif // NETWORKMANAGER_H
