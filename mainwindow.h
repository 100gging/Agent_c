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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum GameState { Menu, GyroCalibrating, Calibrating, HowToPlay, Briefing, Countdown, Playing, GameOver };

    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void gameLoop();
    void fire();
    void onGpioPressed();
    void startGame();
    void enterCalibration();
    void showBriefing();
    void showHowToPlay();
    void showCountdown();
    void startPlaying();
    void retryGame();
    void goToMainMenu();

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

    // Countdown (ready_3, ready_2, ready_1)
    QPixmap ready1Pixmap;
    QPixmap ready2Pixmap;
    QPixmap ready3Pixmap;
    QPixmap loadingPixmap;
    int     countdownPage;      // 0: ready_3, 1: ready_2, 2: ready_1
    QElapsedTimer countdownElapsed;

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
    bool hitEffect;
    int  hitEffectFrames;
    bool lastHitWasEnemy;

    bool calBoxHit[3];
    int  calPhase;     // 0: 영점(사과), 1: 3박스 타겟

    QPushButton *btnStart;
    QPushButton *btnNext;
    QPushButton *btnRetry;
    QPushButton *btnMainMenu;

    MPU6050Sensor m_sensor;
    GpioButton   *m_gpioBtn;

    // 사운드 (ALSA)
    AlsaPlayer *m_audio;
};

#endif // MAINWINDOW_H
