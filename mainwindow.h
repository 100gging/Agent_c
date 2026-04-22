#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPoint>
#include <QTimer>
#include <QPushButton>
#include <QPixmap>
#include <QImage>
#include <QVector>
#include <QMap>
#include <QString>
#include <QElapsedTimer>
#include <QThread>
#include "alsaplayer.h"
#include "mpu6050sensor.h"
#include "gpiobutton.h"
#include "networkmanager.h"
#include "rankingmanager.h"
#include "v4l2camera.h"

struct Target {
    QPoint pos;
    int    speedX;
    int    speedY;
    int    radius;
    int    points;
    bool   isEnemy;
    QString typeName;
    int    dirChangeFrames; // 방향 전환까지 남은 프레임 (지상 유닛용)
};

// 명중 효과 정보 (각 히트 위치에 개별 표시)
struct HitInfo {
    QPoint pos;
    bool   isEnemy;
    int    framesLeft;
};

// 센서 보정용 워커 스레드
class SensorCalibThread : public QThread
{
    Q_OBJECT
public:
    SensorCalibThread(MPU6050Sensor *sensor, QObject *parent = nullptr)
        : QThread(parent), m_sensor(sensor) {}
signals:
    void calibrationDone();
protected:
    void run() override {
        if (m_sensor->init())
        {
            m_sensor->calibrateGyroOffset(500);
            m_sensor->calibrateCenter(200);
        }
        emit calibrationDone();
    }
private:
    MPU6050Sensor *m_sensor;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum GameState { Menu, Calibrating, Story, HowToPlay, Loading, Countdown, Playing, GameOver, Settings, ModeSelect, Ranking, TakingPhoto };
    enum GameMode  { Solo, Cooperative, Competition };

    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void gameLoop();
    void fire();
    void onGpioPressed();
    void onSw2Pressed();
    void onSw3Pressed();
    void startGame();
    void enterCalibration();
    void showStory();
    void showHowToPlay();
    void showLoading();
    void showCountdown();
    void retryGame();
    void goToMainMenu();
    void onCalibrationDone();
    void showModeSelect();
    void onModeReceived(bool cooperative);

    // TCP 동기화: 양쪽 모두 GO 수신 시 게임 시작
    void onSyncGo();

    // TCP 동기화: 연결 끊김 또는 재연결 실패 시 Menu 복귀
    void onConnectionLost();

    // TCP 게임플레이: 에임/발사/상태 수신
    void onAimReceived(int x, int y);
    void onFireReceived();
    void onStateReceived(const QString &stateData);

public:
    void setNetworkManager(NetworkManager *nm);

private:
    bool    isBlockedByWall(QPoint from, QPoint to);
    bool    lineHitsWallMask(QPoint from, QPoint to) const;
    bool    pointHitsOpaquePixel(const QImage &img, const QRect &dstRect, const QPoint &p) const;
    bool    pointHitsTarget(const Target &t, const QPoint &p) const;
    Target  spawnEnemy();
    Target  spawnAlly();
    QPoint  randomPos();
    void    setupUiButtons();
    void    updateButtonLayout();
    void    clampAim();
    void    resetTargets();
    void    resetGame();

    // 멀티플레이어 전용
    int     processHitForPlayer(QPoint aim, bool isServer);
    QString buildStateString(int hitCode);
    void    parseStateString(const QString &s);

private:
    QTimer *timer;
    GameState gameState;

    QPoint aimPos;
    QPoint centerPos;

    int aimStep;
    int aimRadius;

    QVector<Target> targets;

    QMap<QString, QPixmap> pixmaps;
    QMap<QString, QImage> spriteMasks;
    QPixmap backgroundPixmap;
    QPixmap applePixmap;
    QPixmap howToPlay1Pixmap;
    QPixmap howToPlay2Pixmap;
    QPixmap howToPlay3Pixmap;
    int     howToPlayPage;
    int     howToPlayDurationMs;
    QElapsedTimer howToPlayElapsed;

    // Story images
    QPixmap storyPixmaps[5];
    int     storyPage;
    QElapsedTimer storyElapsed;

    // Countdown
    QPixmap ready1Pixmap;
    QPixmap ready2Pixmap;
    QPixmap ready3Pixmap;
    QPixmap loadingPixmap;
    int     countdownPage;
    QElapsedTimer countdownElapsed;

    // Loading state
    QElapsedTimer loadingElapsed;

    // Effect images
    QPixmap attackPixmap;
    QPixmap damEnemyPixmap;
    QPixmap damAllyPixmap;

    // 장애물
    QPixmap treePixmap;
    QPixmap bushPixmap;
    QPixmap leafPixmap;
    QPixmap leafFlippedPixmap;
    QImage treeMaskImage;
    QImage bushMaskImage;
    QImage leafMaskImage;
    QImage leafFlippedMaskImage;

    QRect treeRects[2];
    QRect bushRects[2];
    QRect leafRects[3];

    int score;
    int gameDurationMs;
    QElapsedTimer gameElapsed;

    bool fireEffect;
    QVector<HitInfo> activeHits;

    bool calBoxHit[3];
    int  calPhase;

    QPushButton *btnStart;
    QPushButton *btnRetry;
    QPushButton *btnMainMenu;

    MPU6050Sensor m_sensor;
    SensorCalibThread *m_calibThread;
    bool m_sensorReady;
    GpioButton   *m_gpioBtn;
    GpioButton   *m_sw2Btn;
    GpioButton   *m_sw3Btn;

    int  gameOverCursor;
    QElapsedTimer gameOverTimer;

    int  settingsCursor;
    int  settingsBgmVol;
    int  settingsSfxVol;
    GameState prevState;
    int  pausedRemainingMs;

    // Blackgatmon boss
    QPixmap blackgatmonPixmap;
    QPixmap blackgatmon2Pixmap;
    QPixmap blackgatmon3Pixmap;
    QImage  blackgatmonMask;
    QImage  blackgatmon2Mask;
    QImage  blackgatmon3Mask;
    bool    blackgatmonActive;
    int     blackgatmonSlot;
    float   blackgatmonPopY;
    bool    blackgatmonPopped;
    int     blackgatmonPhase;
    QElapsedTimer blackgatmonPhaseTimer;
    int     blackgatmonNextSpawnMs;
    QElapsedTimer blackgatmonSpawnCooldown;

    // Enemy attack overlay
    QPixmap enemyAttackPixmap;
    bool    enemyAttackActive;
    bool    m_serverEnemyAttackOnly; // true when host attack is caused by client killing blackgatmon (client should NOT show)
    QElapsedTimer enemyAttackTimer;

    AlsaPlayer *m_audio;

    NetworkManager *m_network;
    bool m_waitingForPeer;

    // 멀티플레이어 게임플레이
    QPoint m_peerAimPos;
    int    m_serverScore;
    int    m_clientScore;
    bool   m_clientFirePending;
    int    m_pendingHitCode;
    QVector<Target> m_remoteTargets;
    int    m_remoteRemainMs;

    // 게임 모드 (멀티플레이어)
    GameMode m_gameMode;
    bool     m_modeLocked;  // 서버 측: 모드 선착순 확정 플래그

    // 랭킹
    RankingManager *m_ranking;
    int     m_menuCursor;      // 메뉴 커서: 0=Game Start, 1=Photo, 2=Ranking
    int     m_rankingTab;      // 랭킹 화면 탭: 0=Single, 1=Competitive, 2=Cooperative
    bool    m_rankingSaved;    // GameOver에서 이미 저장했는지 플래그

    // 카메라 촬영
    V4L2Camera m_camera;
    QPixmap    m_cameraPreview;
    QString    m_playerPhotoPath;
};

#endif // MAINWINDOW_H
