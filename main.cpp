#include "mainwindow.h"
#include "networkmanager.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <csignal>

// SIGINT/SIGTERM 시 깔끔한 종료 (소켓 해제 → TIME_WAIT 방지)
static void signalHandler(int)
{
    QCoreApplication::quit();
}

int main(int argc, char *argv[])
{
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    QApplication a(argc, argv);

    // -------------------------------------------------------
    // 커맨드라인 인자 파싱: --server 또는 --client
    //   ./agentC --server   → 서버 역할 (IP: 192.168.10.4)
    //   ./agentC --client   → 클라이언트 역할 (IP: 192.168.10.3)
    //   인자 없음           → 단독 모드 (네트워크 없이 기존 동작)
    // -------------------------------------------------------
    QCommandLineParser parser;
    parser.setApplicationDescription("Agent C - TCP 동기화 슈팅 게임");
    parser.addHelpOption();

    // --server 옵션 정의
    QCommandLineOption serverOption(
        "server",
        "서버 모드로 실행 (라즈베리파이 2, IP: 192.168.10.4)");
    parser.addOption(serverOption);

    // --client 옵션 정의
    QCommandLineOption clientOption(
        "client",
        "클라이언트 모드로 실행 (라즈베리파이 1, IP: 192.168.10.3)");
    parser.addOption(clientOption);

    // 파싱 실행
    parser.process(a);

    bool isServer = parser.isSet(serverOption);
    bool isClient = parser.isSet(clientOption);

    // 메인 윈도우 생성
    MainWindow w;

    if (isServer && isClient) {
        // 두 옵션 동시 지정 시 경고 후 단독 모드로 동작
        qDebug() << "[main] --server와 --client를 동시에 지정할 수 없습니다. 단독 모드로 실행합니다.";

    } else if (isServer) {
        // 서버 모드: NetworkManager 생성 후 MainWindow에 주입
        qDebug() << "[main] 서버 모드로 실행 (포트 5000 listen)";
        NetworkManager *nm = new NetworkManager(NetworkManager::Server);
        w.setNetworkManager(nm); // 소유권도 MainWindow로 이전됨

    } else if (isClient) {
        // 클라이언트 모드: NetworkManager 생성 후 MainWindow에 주입
        qDebug() << "[main] 클라이언트 모드로 실행 (192.168.10.4:5000 connect)";
        NetworkManager *nm = new NetworkManager(NetworkManager::Client);
        w.setNetworkManager(nm); // 소유권도 MainWindow로 이전됨

    } else {
        // 단독 모드: 네트워크 없이 기존 동작
        qDebug() << "[main] 단독 모드 (네트워크 없음). --server 또는 --client 옵션을 사용하세요.";
        w.startBgm();  // 싱글 모드: BGM 즉시 재생
    }

    w.showFullScreen();   // 전체화면
    return a.exec();
}
