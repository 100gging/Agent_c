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
    enum GameState { Menu, GyroCalibrating, Calibrating, Story, HowToPlay, Loading, Countdown, Playing, GameOver, Settings };

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
    void showHowToPlay();
    void showStory();
    void showCountdown();
    void showLoading();
    void retryGame();
    void goToMainMenu();
    void onCalibrationDone();

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

private:
    QTimer *timer;
    GameState gameState;

    QPoint aimPos;
    QPoint centerPos;

    int aimStep;
    int aimRadius;

    // 타겟 리스트 (1개 적군 + 1개 아군)
    QVector<Target> targets;

    // 프리로드된 모든 픽스맵
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

    // Story
    QPixmap storyPixmaps[5];
    int     storyPage;
    QElapsedTimer storyElapsed;

    // Countdown (ready_3, ready_2, ready_1)
    QPixmap ready1Pixmap;
    QPixmap ready2Pixmap;
    QPixmap ready3Pixmap;
    QPixmap loadingPixmap;
    int     countdownPage;      // 0: ready_3, 1: ready_2, 2: ready_1
    QElapsedTimer countdownElapsed;

    // Loading
    QElapsedTimer loadingElapsed;

    // 장애물: tree x2, bush x2, leaf x3 (leafRects[2]는 좌우반전)
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

    // Hit effects: multiple simultaneous hits at monster positions
    struct HitInfo {
        QPoint pos;       // monster center when hit
        bool   isEnemy;
        int    framesLeft;
    };
    QVector<HitInfo> activeHits;

    // Effect images
    QPixmap attackPixmap;
    QPixmap damEnemyPixmap;
    QPixmap damAllyPixmap;

    // Blackgatmon boss enemy
    QPixmap blackgatmonPixmap;
    QPixmap blackgatmon2Pixmap;
    QPixmap blackgatmon3Pixmap;
    QImage  blackgatmonMask;
    QImage  blackgatmon2Mask;
    QImage  blackgatmon3Mask;
    QPixmap enemyAttackPixmap;
    bool    blackgatmonActive;      // is blackgatmon currently on screen
    int     blackgatmonSlot;        // 0=left, 1=center, 2=right
    float   blackgatmonPopY;        // current Y offset for pop-up animation
    bool    blackgatmonPopped;      // true when fully risen
    int     blackgatmonPhase;       // 0=blackgatmon.png, 1=blackgatmon2.png, 2=blackgatmon3.png(descend)
    QElapsedTimer blackgatmonPhaseTimer; // phase duration timer
    QElapsedTimer blackgatmonSpawnCooldown; // time until next spawn
    int     blackgatmonNextSpawnMs;  // random cooldown duration
    bool    enemyAttackActive;      // enemy_attack overlay blocking screen
    QElapsedTimer enemyAttackTimer; // 2s overlay duration

    bool calBoxHit[3];
    int  calPhase;     // 0: 영점(사과), 1: 3박스 타겟

    QPushButton *btnStart;
    QPushButton *btnRetry;
    QPushButton *btnMainMenu;

    MPU6050Sensor m_sensor;
    SensorCalibThread *m_calibThread;
    GpioButton   *m_gpioBtn;   // BCM4: 발사/선택
    GpioButton   *m_sw2Btn;    // BCM17: 아래
    GpioButton   *m_sw3Btn;    // BCM18: 위/설정진입

    // 게임 오버 커서
    int  gameOverCursor;       // 0: Retry, 1: Main Menu
    QElapsedTimer gameOverTimer; // 2초 입력 잠금

    // 환경설정
    int  settingsCursor;       // 0: BGM, 1: SFX, 2: 나가기
    int  settingsBgmVol;       // 0,25,50,75,100
    int  settingsSfxVol;       // 0,25,50,75,100
    GameState prevState;       // 설정 진입 전 상태
    int  pausedRemainingMs;    // 게임 일시정지 시 남은 시간

    // 사운드 (ALSA)
    AlsaPlayer *m_audio;
};

#endif // MAINWINDOW_H
