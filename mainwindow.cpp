#include "mainwindow.h"

#include <QPainter>
#include <QApplication>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QFont>
#include <QPixmap>
#include <QLineF>
#include <QtMath>

static const int BASE_R = 30; // 기본 반지름

// 적군 정의: {typeName, 크기배율, 속도등급(1당 3px), 점수}
struct EnemyDef { QString name; float size; int speedTier; int points; };
static const EnemyDef ENEMY_DEFS[] = {
    {"raremon", 3.0f, 1, 10},
    {"gajimon", 3.0f, 2, 30},
    {"picomon", 3.0f, 3, 50},
    {"oogamon", 6.0f, 2, 70},
};
static const EnemyDef ALLY_DEFS[] = {
    {"padakmon",  2.0f, 1, -10},
    {"dongulmon", 2.0f, 2, -30},
    {"agumon",    3.0f, 2, -50},
    {"tentamon",  3.0f, 3, -30},
    {"metamon",   2.0f, 1, -10},
};

static bool isGroundUnit(const QString &name) {
    return name == "agumon" || name == "dongulmon" || name == "metamon"
        || name == "oogamon" || name == "raremon";
}

static int randomDirChangeFrames() {
    return QRandomGenerator::global()->bounded(66, 101);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      timer(new QTimer(this)),
      gameState(Menu),
      aimStep(20),
      aimRadius(18),
      score(0),
      gameDurationMs(30000),
      fireEffect(false),
      btnStart(nullptr),
      btnRetry(nullptr),
      btnMainMenu(nullptr),
      m_calibThread(nullptr),
      m_sensorReady(false),
      m_network(nullptr),
      m_waitingForPeer(false),
      m_serverScore(0),
      m_clientScore(0),
      m_clientFirePending(false),
      m_pendingHitCode(0),
      m_remoteRemainMs(30000),
      m_gpioBtn(new GpioButton(4, this)),
      m_sw2Btn(new GpioButton(17, this)),
      m_sw3Btn(new GpioButton(18, this)),
      gameOverCursor(0),
      settingsCursor(0),
      settingsBgmVol(100),
      settingsSfxVol(100),
      prevState(Menu),
      pausedRemainingMs(0),
      blackgatmonActive(false),
      blackgatmonSlot(0),
      blackgatmonPopY(0),
      blackgatmonPopped(false),
      blackgatmonPhase(0),
      blackgatmonNextSpawnMs(7000),
      enemyAttackActive(false),
      m_serverEnemyAttackOnly(false)
{
    resize(1024, 600);
    setWindowTitle("Agent C");
    setStyleSheet("background-color: black;");

    centerPos = QPoint(width() / 2, height() / 2);
    aimPos = centerPos;

    // 모든 이미지 프리로드 (runtime loading)
    for (auto &d : ENEMY_DEFS)
        pixmaps[d.name] = QPixmap(QString("images/%1.png").arg(d.name));
    for (auto &d : ALLY_DEFS)
        pixmaps[d.name] = QPixmap(QString("images/%1.png").arg(d.name));

    for (auto it = pixmaps.constBegin(); it != pixmaps.constEnd(); ++it) {
        if (!it.value().isNull())
            spriteMasks[it.key()] = it.value().toImage().convertToFormat(QImage::Format_ARGB32);
    }

    backgroundPixmap = QPixmap("images/background_game.png");
    applePixmap = QPixmap("images/apple.png");

    howToPlay1Pixmap = QPixmap("images/how_to_play1.png");
    howToPlay2Pixmap = QPixmap("images/how_to_play2.png");
    howToPlay3Pixmap = QPixmap("images/how_to_play3.png");
    howToPlayPage = 0;
    howToPlayDurationMs = 3000;

    // Story images
    for (int i = 0; i < 5; i++)
        storyPixmaps[i] = QPixmap(QString("images/story%1.png").arg(i + 1));
    storyPage = 0;

    ready1Pixmap = QPixmap("images/ready_1.png");
    ready2Pixmap = QPixmap("images/ready_2.png");
    ready3Pixmap = QPixmap("images/ready_3.png");
    loadingPixmap = QPixmap("images/loading.png");
    countdownPage = 0;

    // Effect images (attack, dam_enemy, dam_ally)
    attackPixmap = QPixmap("images/attack.png");
    damEnemyPixmap = QPixmap("images/dam_enemy.png");
    damAllyPixmap = QPixmap("images/dam_ally.png");

    // Remove background from effect images
    auto removeEffectBg = [](QPixmap &pm) {
        if (pm.isNull()) return;
        QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
        int w = img.width(), h = img.height();
        for (int py = 0; py < h; ++py) {
            QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(py));
            for (int px = 0; px < w; ++px) {
                int r = qRed(line[px]), g = qGreen(line[px]), b = qBlue(line[px]);
                int maxC = qMax(r, qMax(g, b));
                int minC = qMin(r, qMin(g, b));
                if ((maxC - minC) < 35 && minC > 140) {
                    line[px] = qRgba(0, 0, 0, 0);
                    continue;
                }
                if (maxC < 50) {
                    line[px] = qRgba(0, 0, 0, 0);
                }
            }
        }
        pm = QPixmap::fromImage(img);
    };
    removeEffectBg(attackPixmap);
    removeEffectBg(damEnemyPixmap);
    removeEffectBg(damAllyPixmap);

    // Blackgatmon & enemy_attack
    blackgatmonPixmap = QPixmap("images/blackgatmon.png");
    blackgatmon2Pixmap = QPixmap("images/blackgatmon2.png");
    blackgatmon3Pixmap = QPixmap("images/blackgatmon3.png");
    enemyAttackPixmap = QPixmap("images/enemy_attack.png");
    if (!blackgatmonPixmap.isNull())
        blackgatmonMask = blackgatmonPixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    if (!blackgatmon2Pixmap.isNull())
        blackgatmon2Mask = blackgatmon2Pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    removeEffectBg(blackgatmon3Pixmap);
    if (!blackgatmon3Pixmap.isNull())
        blackgatmon3Mask = blackgatmon3Pixmap.toImage().convertToFormat(QImage::Format_ARGB32);

    // apple.png 격자무늬(체커보드) 배경 제거 + 절반 크기
    if (!applePixmap.isNull()) {
        QImage appleImg = applePixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        int aw = appleImg.width(), ah = appleImg.height();
        for (int py = 0; py < ah; ++py) {
            QRgb *line = reinterpret_cast<QRgb*>(appleImg.scanLine(py));
            for (int px = 0; px < aw; ++px) {
                int r = qRed(line[px]), g = qGreen(line[px]), b = qBlue(line[px]);
                int maxC = qMax(r, qMax(g, b));
                int minC = qMin(r, qMin(g, b));
                if ((maxC - minC) < 30 && minC > 170) {
                    line[px] = qRgba(0, 0, 0, 0);
                }
            }
        }
        applePixmap = QPixmap::fromImage(appleImg).scaled(
            applePixmap.width() / 2, applePixmap.height() / 2,
            Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    treePixmap = QPixmap("images/tree.png");
    bushPixmap = QPixmap("images/bush.png");
    leafPixmap = QPixmap("images/leaf.png");

    // 장애물 이미지에 검정 테두리 추가 (scanLine + distance map 최적화)
    auto addOutline = [](QPixmap &pm, int thickness = 25) {
        if (pm.isNull()) return;
        QImage src = pm.toImage().convertToFormat(QImage::Format_ARGB32);
        int w = src.width(), h = src.height();

        // 1단계: 불투명 여부 배열 생성
        QVector<bool> opaque(w * h);
        for (int y = 0; y < h; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            for (int x = 0; x < w; ++x)
                opaque[y * w + x] = (qAlpha(line[x]) > 10);
        }

        // 2단계: 거리맵 — 각 픽셀에서 가장 가까운 불투명 픽셀까지의 거리²
        // 2-pass (행 방향 + 열 방향) Euclidean distance transform 근사
        const int INF = w * w + h * h;
        QVector<int> dist(w * h, INF);

        // 불투명 픽셀은 거리 0
        for (int i = 0; i < w * h; ++i)
            if (opaque[i]) dist[i] = 0;

        // 좌상→우하 패스
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int idx = y * w + x;
                if (x > 0) dist[idx] = qMin(dist[idx], dist[idx - 1] + 1);
                if (y > 0) dist[idx] = qMin(dist[idx], dist[(y-1) * w + x] + 1);
            }
        }
        // 우하→좌상 패스
        for (int y = h - 1; y >= 0; --y) {
            for (int x = w - 1; x >= 0; --x) {
                int idx = y * w + x;
                if (x < w - 1) dist[idx] = qMin(dist[idx], dist[idx + 1] + 1);
                if (y < h - 1) dist[idx] = qMin(dist[idx], dist[(y+1) * w + x] + 1);
            }
        }

        // 3단계: 투명이고 거리 <= thickness인 픽셀에 검정 테두리
        QImage dst = src.copy();
        for (int y = 0; y < h; ++y) {
            QRgb *dstLine = reinterpret_cast<QRgb*>(dst.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int idx = y * w + x;
                if (!opaque[idx] && dist[idx] <= thickness)
                    dstLine[x] = qRgba(0, 0, 0, 220);
            }
        }
        pm = QPixmap::fromImage(dst);
    };
    addOutline(treePixmap);
    addOutline(bushPixmap);
    addOutline(leafPixmap);

    // 좌우 반전 leaf
    if (!leafPixmap.isNull()) {
        leafFlippedPixmap = QPixmap::fromImage(leafPixmap.toImage().mirrored(true, false));
        leafFlippedMaskImage = leafFlippedPixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    }

    if (!treePixmap.isNull())
        treeMaskImage = treePixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    if (!bushPixmap.isNull())
        bushMaskImage = bushPixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    if (!leafPixmap.isNull())
        leafMaskImage = leafPixmap.toImage().convertToFormat(QImage::Format_ARGB32);

    // 초기 벽 위치 설정
    treeRects[0] = QRect(60, 260, 180, 320);
    treeRects[1] = QRect(780, 240, 160, 300);
    bushRects[0] = QRect(744, 230, 200, 140);
    bushRects[1] = QRect(120, 200, 180, 130);
    leafRects[0] = QRect(272, -20, 240, 140);
    leafRects[1] = QRect(500, 30, 200, 130);
    leafRects[2] = QRect(700, 10, 200, 130);

    setupUiButtons();
    resetTargets();

    // ALSA 사운드 초기화
    m_audio = new AlsaPlayer(this);
    m_audio->loadSfx("fire", "sounds/fire.wav", 150);
    m_audio->loadSfx("enemy_dead", "sounds/enemy_dead.wav");
    m_audio->loadSfx("ally_dead", "sounds/ally_dead.wav");
    m_audio->loadBgm("menu", "sounds/butterfly.wav");
    m_audio->setBgmVolume(100);
    m_audio->loadBgm("game", "sounds/gamebgm.wav");
    m_audio->playBgm("menu");

    connect(timer, &QTimer::timeout, this, &MainWindow::gameLoop);
    timer->start(30);

    m_sensor.setScreenSize(1024, 600);

    connect(m_gpioBtn, &GpioButton::pressed, this, &MainWindow::onGpioPressed);
    m_gpioBtn->start(50);

    connect(m_sw2Btn, &GpioButton::pressed, this, &MainWindow::onSw2Pressed);
    m_sw2Btn->start(50);

    connect(m_sw3Btn, &GpioButton::pressed, this, &MainWindow::onSw3Pressed);
    m_sw3Btn->start(50);
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUiButtons()
{
    btnStart = new QPushButton("GAME START", this);
    btnRetry = new QPushButton("RETRY", this);
    btnMainMenu = new QPushButton("MAIN MENU", this);

    QString commonStyle =
        "QPushButton {"
        "  background-color: rgba(80, 80, 80, 180);"
        "  color: white;"
        "  border: 2px solid white;"
        "  border-radius: 12px;"
        "  font-size: 20px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(180, 180, 180, 200);"
        "  color: black;"
        "}";

    QString startStyle =
        "QPushButton {"
        "  background-color: rgba(30, 130, 30, 220);"
        "  color: white;"
        "  border: 2px solid white;"
        "  border-radius: 20px;"
        "  font-size: 28px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(100, 200, 100, 230);"
        "  color: black;"
        "}";

    btnRetry->setStyleSheet(commonStyle);
    btnMainMenu->setStyleSheet(commonStyle);
    btnStart->setStyleSheet(startStyle);

    for (QPushButton *btn : {btnStart, btnRetry, btnMainMenu}) {
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setAutoRepeat(false);
    }

    connect(btnStart,    &QPushButton::pressed, this, &MainWindow::startGame);
    connect(btnRetry,    &QPushButton::pressed, this, &MainWindow::retryGame);
    connect(btnMainMenu, &QPushButton::pressed, this, &MainWindow::goToMainMenu);

    updateButtonLayout();
}

// ─── spawnEnemy / spawnAlly / randomPos ───────────────────────────
QPoint MainWindow::randomPos()
{
    int margin = 140;
    int minX = margin;
    int maxX = qMax(minX + 1, width()  - margin);
    int minY = 110;
    int maxY = qMax(minY + 1, height() - 140);
    return QPoint(
        QRandomGenerator::global()->bounded(minX, maxX),
        QRandomGenerator::global()->bounded(minY, maxY)
    );
}

Target MainWindow::spawnEnemy()
{
    int idx = QRandomGenerator::global()->bounded((int)(sizeof(ENEMY_DEFS)/sizeof(ENEMY_DEFS[0])));
    const EnemyDef &d = ENEMY_DEFS[idx];
    int spd = d.speedTier * 3;
    int sx  = QRandomGenerator::global()->bounded(2) ? spd : -spd;
    int sy  = QRandomGenerator::global()->bounded(2) ? spd : -spd;
    Target t;
    t.pos = randomPos();
    t.speedX = sx;
    t.speedY = sy;
    t.radius = (int)(BASE_R * d.size);
    t.points = d.points;
    t.isEnemy = true;
    t.typeName = d.name;
    t.dirChangeFrames = randomDirChangeFrames();
    if (isGroundUnit(d.name)) {
        int groundMinY = height() / 2 - 20;
        int groundMaxY = qMax(groundMinY + 1, height() - 140);
        t.pos.setY(QRandomGenerator::global()->bounded(groundMinY, groundMaxY));
    }
    return t;
}

Target MainWindow::spawnAlly()
{
    int idx = QRandomGenerator::global()->bounded((int)(sizeof(ALLY_DEFS)/sizeof(ALLY_DEFS[0])));
    const EnemyDef &d = ALLY_DEFS[idx];
    int spd = d.speedTier * 3;
    int sx  = QRandomGenerator::global()->bounded(2) ? spd : -spd;
    int sy  = QRandomGenerator::global()->bounded(2) ? spd : -spd;
    Target t;
    t.pos = randomPos();
    t.speedX = sx;
    t.speedY = sy;
    t.radius = (int)(BASE_R * d.size);
    t.points = d.points;
    t.isEnemy = false;
    t.typeName = d.name;
    t.dirChangeFrames = randomDirChangeFrames();
    if (isGroundUnit(d.name)) {
        int groundMinY = height() / 2 - 20;
        int groundMaxY = qMax(groundMinY + 1, height() - 140);
        t.pos.setY(QRandomGenerator::global()->bounded(groundMinY, groundMaxY));
    }
    return t;
}

void MainWindow::resetTargets()
{
    targets.clear();
    targets.append(spawnEnemy());
    targets.append(spawnEnemy());
    targets.append(spawnEnemy());
    targets.append(spawnAlly());
    targets.append(spawnAlly());
}

bool MainWindow::isBlockedByWall(QPoint from, QPoint to)
{
    return lineHitsWallMask(from, to);
}

bool MainWindow::pointHitsOpaquePixel(const QImage &img, const QRect &dstRect, const QPoint &p) const
{
    if (!dstRect.contains(p)) return false;
    if (img.isNull() || !img.hasAlphaChannel()) return true;

    int rw = qMax(1, dstRect.width());
    int rh = qMax(1, dstRect.height());
    int rx = p.x() - dstRect.left();
    int ry = p.y() - dstRect.top();

    int sx = qBound(0, (rx * img.width()) / rw, img.width() - 1);
    int sy = qBound(0, (ry * img.height()) / rh, img.height() - 1);

    return qAlpha(img.pixel(sx, sy)) > 10;
}

bool MainWindow::lineHitsWallMask(QPoint from, QPoint to) const
{
    int dx = to.x() - from.x();
    int dy = to.y() - from.y();
    int steps = qMax(qAbs(dx), qAbs(dy));

    auto pointBlocked = [&](const QPoint &p) {
        for (int i = 0; i < 2; i++)
            if (pointHitsOpaquePixel(treeMaskImage, treeRects[i], p)) return true;
        for (int i = 0; i < 2; i++)
            if (pointHitsOpaquePixel(bushMaskImage, bushRects[i], p)) return true;
        for (int i = 0; i < 2; i++)
            if (pointHitsOpaquePixel(leafMaskImage, leafRects[i], p)) return true;
        if (pointHitsOpaquePixel(leafFlippedMaskImage, leafRects[2], p)) return true;
        return false;
    };

    if (steps <= 0) return pointBlocked(from);

    for (int i = 0; i <= steps; ++i) {
        qreal t = static_cast<qreal>(i) / steps;
        QPoint p(
            qRound(from.x() + dx * t),
            qRound(from.y() + dy * t)
        );
        if (pointBlocked(p)) return true;
    }
    return false;
}

bool MainWindow::pointHitsTarget(const Target &t, const QPoint &p) const
{
    QRect targetRect(t.pos.x() - t.radius, t.pos.y() - t.radius, t.radius * 2, t.radius * 2);

    auto it = spriteMasks.find(t.typeName);
    if (it != spriteMasks.end()) {
        return pointHitsOpaquePixel(it.value(), targetRect, p);
    }

    int dx = p.x() - t.pos.x();
    int dy = p.y() - t.pos.y();
    return dx * dx + dy * dy <= t.radius * t.radius;
}


void MainWindow::updateButtonLayout()
{
    if (!btnStart) return;

    int w = width();
    int h = height();

    btnRetry->setGeometry(w / 2 - 220, h / 2 + 80, 200, 70);
    btnMainMenu->setGeometry(w / 2 + 20, h / 2 + 80, 200, 70);
    btnStart->setGeometry(w / 2 - 140, h / 2 + 40, 280, 90);

    bool isMenu     = (gameState == Menu);
    bool isGameOver = (gameState == GameOver);

    btnStart->setVisible(isMenu);
    btnRetry->setVisible(isGameOver);
    btnMainMenu->setVisible(isGameOver);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    centerPos = QPoint(width() / 2, height() / 2);

    treeRects[0] = QRect(90, height() / 2 - 60, 180, 320);
    treeRects[1] = QRect(width() - 220, height() - 310, 160, 300);
    bushRects[0] = QRect(225, height() / 2 + 35, 250, 175);
    bushRects[1] = QRect(width() - 500, height() / 2 - 40, 200, 140);
    leafRects[0] = QRect(width() / 2 - 240, 85, 120, 140);
    leafRects[1] = QRect(width() - 250, 60, 170, 180);
    leafRects[2] = QRect(width() / 2 - 90, 30, 150, 160);
    updateButtonLayout();
    clampAim();
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 외곽선 텍스트 헬퍼
    auto drawOutlinedText = [&](const QRect &rect, int flags, const QString &text) {
        painter.setPen(QColor(255, 255, 255, 180));
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
                if (dx || dy)
                    painter.drawText(rect.adjusted(dx, dy, dx, dy), flags, text);
        painter.setPen(Qt::black);
        painter.drawText(rect, flags, text);
    };

    // 배경
    if (!backgroundPixmap.isNull())
        painter.drawPixmap(rect(), backgroundPixmap);
    else
        painter.fillRect(rect(), QColor(10, 10, 10));

    QFont infoFont("Arial", 18, QFont::Bold);
    QFont bigFont("Arial", 36, QFont::Bold);
    QFont subFont("Arial", 20);

    if (gameState == Menu) {
        painter.setFont(bigFont);
        painter.setPen(Qt::white);
        painter.drawText(rect().adjusted(0, -80, 0, 0), Qt::AlignCenter, "AGENT C");

        painter.setFont(subFont);
        drawOutlinedText(rect().adjusted(0, 40, 0, 0), Qt::AlignCenter,
                         "Press BUTTON to begin");

        painter.setFont(QFont("Arial", 13));
        drawOutlinedText(QRect(0, height() - 160, width(), 60), Qt::AlignCenter,
                         "After pressing, hold the gun still and aim at the screen center.\n"
                         "The sensor will calibrate automatically. (~5 sec)");
        return;
    }

    if (gameState == Settings) {
        if (!backgroundPixmap.isNull())
            painter.drawPixmap(rect(), backgroundPixmap);
        painter.fillRect(rect(), QColor(0, 0, 0, 160));

        painter.setFont(bigFont);
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(rect().adjusted(0, -200, 0, 0), Qt::AlignCenter, "SETTINGS");

        QFont itemFont("Arial", 24, QFont::Bold);
        painter.setFont(itemFont);

        int startY = height() / 2 - 90;
        int lineH = 70;

        QString items[4] = {
            QString("BGM Volume: %1").arg(settingsBgmVol),
            QString("SFX Volume: %1").arg(settingsSfxVol),
            QString("RECALIBRATE"),
            QString("EXIT")
        };

        for (int i = 0; i < 4; i++) {
            QRect itemRect(width() / 2 - 200, startY + i * lineH, 400, 50);

            if (i == settingsCursor) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(60, 160, 60, 150));
                painter.drawRoundedRect(itemRect, 10, 10);
                painter.setPen(QColor(100, 255, 100));
                painter.drawText(itemRect, Qt::AlignCenter, QString("> %1 <").arg(items[i]));
            } else {
                painter.setPen(QColor(180, 180, 180));
                painter.drawText(itemRect, Qt::AlignCenter, items[i]);
            }
        }

        painter.setFont(QFont("Arial", 14));
        drawOutlinedText(rect().adjusted(0, 350, 0, 0), Qt::AlignCenter,
                         "SW2: DOWN  |  SW3: UP  |  FIRE: SELECT");
        return;
    }

    if (gameState == Story) {
        const QPixmap &pm = storyPixmaps[storyPage];
        if (!pm.isNull())
            painter.drawPixmap(rect(), pm);
        else
            painter.fillRect(rect(), QColor(10, 10, 10));

        // Story5 (storyPage==4): show crosshair + START MISSION button
        if (storyPage == 4) {
            int bw = 280, bh = 70;
            QRect startBtn(width() / 2 - bw / 2, height() - 90, bw, bh);
            painter.setPen(QPen(Qt::white, 2));
            painter.setBrush(QColor(180, 30, 30, 220));
            painter.drawRoundedRect(startBtn, 12, 12);
            painter.setPen(Qt::white);
            painter.setFont(QFont("Arial", 22, QFont::Bold));
            painter.drawText(startBtn, Qt::AlignCenter, "START MISSION");

            // 네트워크 모드에서 상대방 대기 중 표시
            if (m_waitingForPeer) {
                painter.setFont(QFont("Arial", 24, QFont::Bold));
                painter.setPen(QColor(255, 220, 50));
                painter.drawText(QRect(0, height() - 170, width(), 40), Qt::AlignCenter,
                                 "Waiting for opponent...");
            }

            {
                QColor storyAimColor = (m_network && m_network->role() == NetworkManager::Client)
                    ? QColor(100, 200, 255) : Qt::green;
                painter.setPen(QPen(storyAimColor, 3));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(aimPos, aimRadius, aimRadius);
                painter.drawLine(aimPos.x() - 25, aimPos.y(), aimPos.x() + 25, aimPos.y());
                painter.drawLine(aimPos.x(), aimPos.y() - 25, aimPos.x(), aimPos.y() + 25);
            }
        }
        return;
    }

    if (gameState == HowToPlay) {
        const QPixmap &pm = (howToPlayPage == 0) ? howToPlay1Pixmap
                          : (howToPlayPage == 1) ? howToPlay2Pixmap
                          : howToPlay3Pixmap;
        if (!pm.isNull())
            painter.drawPixmap(rect(), pm);
        else
            painter.fillRect(rect(), QColor(10, 10, 10));
        return;
    }

    if (gameState == Loading) {
        if (!loadingPixmap.isNull())
            painter.drawPixmap(rect(), loadingPixmap);
        else {
            painter.fillRect(rect(), QColor(10, 10, 10));
            painter.setFont(bigFont);
            painter.setPen(Qt::white);
            painter.drawText(rect(), Qt::AlignCenter, "LOADING...");
        }
        return;
    }

    if (gameState == Countdown) {
        const QPixmap &pm = (countdownPage == 0) ? ready3Pixmap
                          : (countdownPage == 1) ? ready2Pixmap
                          : ready1Pixmap;
        if (!pm.isNull())
            painter.drawPixmap(rect(), pm);
        else
            painter.fillRect(rect(), QColor(10, 10, 10));
        return;
    }

    if (gameState == Calibrating) {
        painter.setFont(bigFont);
        painter.setPen(Qt::white);
        painter.drawText(rect().adjusted(0, -100, 0, 0), Qt::AlignCenter, "CALIBRATION");

        if (calPhase == 0) {
            painter.setFont(subFont);
            if (!m_sensorReady) {
                drawOutlinedText(rect().adjusted(0, 100, 0, 0), Qt::AlignCenter,
                                 "센서 보정 중입니다. 사과를 조준하고 가만히 계세요...");
            } else {
                drawOutlinedText(rect().adjusted(0, 100, 0, 0), Qt::AlignCenter,
                                 "Aim at the apple and press FIRE to zero in");
            }

            QPoint calTarget(width() / 2, height() / 2);
            if (!applePixmap.isNull()) {
                int ax = calTarget.x() - applePixmap.width() / 2;
                int ay = calTarget.y() - applePixmap.height() / 2;
                painter.drawPixmap(ax, ay, applePixmap);
            } else {
                painter.setPen(QPen(Qt::red, 3));
                painter.setBrush(QColor(200, 40, 40, 120));
                painter.drawEllipse(calTarget, 30, 30);
            }
            if (m_sensorReady) {
                QColor calAimColor = (m_network && m_network->role() == NetworkManager::Client)
                    ? QColor(100, 200, 255) : Qt::green;
                painter.setPen(QPen(calAimColor, 3));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(aimPos, aimRadius, aimRadius);
                painter.drawLine(aimPos.x() - 30, aimPos.y(), aimPos.x() + 30, aimPos.y());
                painter.drawLine(aimPos.x(), aimPos.y() - 30, aimPos.x(), aimPos.y() + 30);
            }
        } else {
            painter.setFont(subFont);
            drawOutlinedText(rect().adjusted(0, 60, 0, 0), Qt::AlignCenter,
                             "Shoot all 3 targets to start the mission!");

            int boxW = 70, boxH = 50;
            QRect calBoxes[3] = {
                QRect(width() / 4 - boxW / 2, height() - 180, boxW, boxH),
                QRect(width() / 2 - boxW / 2, 120, boxW, boxH),
                QRect(3 * width() / 4 - boxW / 2, height() - 180, boxW, boxH),
            };

            for (int i = 0; i < 3; i++) {
                if (!calBoxHit[i]) {
                    painter.setPen(QPen(Qt::red, 3));
                    painter.setBrush(QColor(200, 40, 40, 120));
                    painter.drawRect(calBoxes[i]);
                    if (!applePixmap.isNull()) {
                        int ax = calBoxes[i].center().x() - applePixmap.width() / 2;
                        int ay = calBoxes[i].center().y() - applePixmap.height() / 2;
                        painter.drawPixmap(ax, ay, applePixmap);
                    }
                } else {
                    painter.setPen(QPen(QColor(80, 200, 80), 3));
                    painter.setBrush(QColor(80, 200, 80, 40));
                    painter.drawRect(calBoxes[i]);
                    painter.setFont(QFont("Arial", 24, QFont::Bold));
                    painter.setPen(QColor(80, 200, 80));
                    painter.drawText(calBoxes[i], Qt::AlignCenter, "✓");
                }
            }

            {
                QColor calAimColor = (m_network && m_network->role() == NetworkManager::Client)
                    ? QColor(100, 200, 255) : Qt::green;
                painter.setPen(QPen(calAimColor, 3));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(aimPos, aimRadius, aimRadius);
                painter.drawLine(aimPos.x() - 30, aimPos.y(), aimPos.x() + 30, aimPos.y());
                painter.drawLine(aimPos.x(), aimPos.y() - 30, aimPos.x(), aimPos.y() + 30);
            }
        }
        return;
    }

    // --- Playing / GameOver ---

    // 상단 정보
    painter.setFont(infoFont);
    if (m_network) {
        painter.setPen(Qt::green);
        painter.drawText(20, 35, QString("Player 1: %1").arg(m_serverScore));
        painter.setPen(QColor(100, 200, 255));
        painter.drawText(20, 65, QString("Player 2: %1").arg(m_clientScore));
    } else {
        painter.setPen(Qt::white);
        painter.drawText(20, 35, QString("Score: %1").arg(score));
    }
    int remainingTimeMs;
    if (m_network && m_network->role() == NetworkManager::Client)
        remainingTimeMs = m_remoteRemainMs;
    else
        remainingTimeMs = qMax(0, gameDurationMs - (int)gameElapsed.elapsed());
    painter.setPen(Qt::white);
    QString timeStr = QString("Time: %1").arg(remainingTimeMs / 1000);
    painter.drawText(width() - 180, 35, timeStr);

    // 1. 타겟
    const QVector<Target> &renderTargets =
        (m_network && m_network->role() == NetworkManager::Client)
        ? m_remoteTargets : targets;
    if (gameState == Playing || gameState == GameOver) {
        for (const Target &t : renderTargets) {
            const QPixmap pm = pixmaps.value(t.typeName);
            if (!pm.isNull()) {
                int d = t.radius * 2;
                painter.drawPixmap(t.pos.x() - t.radius, t.pos.y() - t.radius, d, d, pm);
            } else {
                QColor fill = t.isEnemy ? QColor(200, 40, 40) : QColor(60, 120, 220);
                painter.setBrush(fill);
                painter.setPen(QPen(Qt::white, 2));
                painter.drawEllipse(t.pos, t.radius, t.radius);
                painter.setPen(Qt::white);
                painter.drawText(t.pos.x() - t.radius / 2, t.pos.y() + 5,
                                 t.isEnemy ? "ENEMY" : "ALLY");
            }
        }
    }

    // 2. 장애물
    for (int i = 0; i < 2; i++) {
        if (!treePixmap.isNull())
            painter.drawPixmap(treeRects[i], treePixmap);
        else {
            painter.setBrush(QColor(80, 60, 40));
            painter.setPen(Qt::NoPen);
            painter.drawRect(treeRects[i]);
        }
    }

    for (int i = 0; i < 2; i++) {
        if (!bushPixmap.isNull())
            painter.drawPixmap(bushRects[i], bushPixmap);
        else {
            painter.setBrush(QColor(30, 100, 40));
            painter.setPen(Qt::NoPen);
            painter.drawRect(bushRects[i]);
        }
    }

    for (int i = 0; i < 2; i++) {
        if (!leafPixmap.isNull())
            painter.drawPixmap(leafRects[i], leafPixmap);
        else {
            painter.setBrush(QColor(40, 140, 60));
            painter.setPen(Qt::NoPen);
            painter.drawRect(leafRects[i]);
        }
    }
    if (!leafFlippedPixmap.isNull())
        painter.drawPixmap(leafRects[2], leafFlippedPixmap);
    else {
        painter.setBrush(QColor(40, 140, 60));
        painter.setPen(Qt::NoPen);
        painter.drawRect(leafRects[2]);
    }

    // 로컬 에임
    QColor localAimColor = (m_network && m_network->role() == NetworkManager::Client)
        ? QColor(100, 200, 255) : Qt::green;
    painter.setPen(QPen(localAimColor, 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(aimPos, aimRadius, aimRadius);
    painter.drawLine(aimPos.x() - 25, aimPos.y(), aimPos.x() + 25, aimPos.y());
    painter.drawLine(aimPos.x(), aimPos.y() - 25, aimPos.x(), aimPos.y() + 25);

    // 상대방 에임 (멀티 모드)
    if (m_network) {
        QColor peerAimColor = (m_network->role() == NetworkManager::Client)
            ? Qt::green : QColor(100, 200, 255);
        painter.setPen(QPen(peerAimColor, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(m_peerAimPos, aimRadius, aimRadius);
        painter.drawLine(m_peerAimPos.x() - 25, m_peerAimPos.y(), m_peerAimPos.x() + 25, m_peerAimPos.y());
        painter.drawLine(m_peerAimPos.x(), m_peerAimPos.y() - 25, m_peerAimPos.x(), m_peerAimPos.y() + 25);
    }

    // 발사 효과
    if (fireEffect) {
        if (!attackPixmap.isNull()) {
            int aw = 80;
            int ah = (int)(80.0 * attackPixmap.height() / qMax(1, attackPixmap.width()));
            painter.drawPixmap(aimPos.x() - aw / 2, aimPos.y() - ah / 2, aw, ah, attackPixmap);
        } else {
            painter.setPen(QPen(Qt::yellow, 4));
            painter.drawEllipse(aimPos, aimRadius + 10, aimRadius + 10);
        }
    }

    // 명중 효과 (각 히트 위치에 개별 표시)
    for (const HitInfo &hi : activeHits) {
        const QPixmap &damPm = hi.isEnemy ? damEnemyPixmap : damAllyPixmap;
        if (!damPm.isNull()) {
            int dw = hi.isEnemy ? 130 : 90;
            int dh = (int)((double)dw * damPm.height() / qMax(1, damPm.width()));
            painter.drawPixmap(hi.pos.x() - dw / 2, hi.pos.y() - dh / 2, dw, dh, damPm);
        } else {
            painter.setPen(QPen(Qt::white, 5));
            QColor hc = hi.isEnemy ? QColor(255, 200, 0, 160) : QColor(80, 180, 255, 160);
            painter.setBrush(hc);
            painter.drawEllipse(hi.pos, aimRadius + 20, aimRadius + 20);
            painter.setPen(hi.isEnemy ? Qt::yellow : Qt::cyan);
            painter.setFont(QFont("Arial", 22, QFont::Bold));
            painter.drawText(hi.pos.x() + 25, hi.pos.y() - 30,
                             hi.isEnemy ? "+HIT!" : "-ALLY!");
        }
    }

    // Blackgatmon 렌더링
    if (blackgatmonActive) {
        int bgSize = 300;
        int slotX;
        if (blackgatmonSlot == 0)      slotX = width() / 4 - bgSize / 2;
        else if (blackgatmonSlot == 1)  slotX = width() / 2 - bgSize / 2;
        else                            slotX = 3 * width() / 4 - bgSize / 2;
        const QPixmap &bgPm = (blackgatmonPhase == 2) ? blackgatmon3Pixmap
                            : (blackgatmonPhase == 1) ? blackgatmon2Pixmap
                            : blackgatmonPixmap;
        if (!bgPm.isNull())
            painter.drawPixmap(slotX, (int)blackgatmonPopY, bgSize, bgSize, bgPm);
    }

    // enemy_attack 전체 화면 오버레이
    if (enemyAttackActive && !enemyAttackPixmap.isNull()) {
        painter.drawPixmap(rect(), enemyAttackPixmap);
    }

    // 게임 종료 화면
    if (gameState == GameOver) {
        painter.fillRect(rect(), QColor(0, 0, 0, 180));
        painter.setFont(bigFont);
        painter.setPen(Qt::white);
        painter.drawText(rect().adjusted(0, -100, 0, 0), Qt::AlignCenter, "GAME OVER");

        if (m_network) {
            QString winText;
            QColor  winColor;
            if      (m_serverScore > m_clientScore) { winText = "Player 1 WINS!";  winColor = Qt::green; }
            else if (m_clientScore > m_serverScore) { winText = "Player 2 WINS!"; winColor = QColor(100, 200, 255); }
            else                                    { winText = "DRAW!";       winColor = Qt::yellow; }
            painter.setFont(QFont("Arial", 30, QFont::Bold));
            painter.setPen(winColor);
            painter.drawText(rect().adjusted(0, -30, 0, 0), Qt::AlignCenter, winText);
            painter.setFont(QFont("Arial", 22, QFont::Bold));
            painter.setPen(Qt::green);
            painter.drawText(rect().adjusted(0, 20, 0, 0), Qt::AlignCenter,
                             QString("Player 1: %1").arg(m_serverScore));
            painter.setPen(QColor(100, 200, 255));
            painter.drawText(rect().adjusted(0, 55, 0, 0), Qt::AlignCenter,
                             QString("Player 2: %1").arg(m_clientScore));
        } else {
            painter.setFont(QFont("Arial", 26, QFont::Bold));
            painter.setPen(QColor(255, 220, 50));
            painter.drawText(rect().adjusted(0, -20, 0, 0), Qt::AlignCenter,
                             QString("Final Score: %1").arg(score));
        }

        // GameOver 선택 커서 하이라이트
        QPushButton *goButtons[2] = { btnRetry, btnMainMenu };
        QString selStyle =
            "QPushButton {"
            "  background-color: rgba(60, 160, 60, 200);"
            "  color: white;"
            "  border: 3px solid #66ff66;"
            "  border-radius: 12px;"
            "  font-size: 20px;"
            "  font-weight: bold;"
            "}";
        QString normStyle =
            "QPushButton {"
            "  background-color: rgba(80, 80, 80, 180);"
            "  color: white;"
            "  border: 2px solid white;"
            "  border-radius: 12px;"
            "  font-size: 20px;"
            "  font-weight: bold;"
            "}";
        for (int i = 0; i < 2; i++)
            goButtons[i]->setStyleSheet(i == gameOverCursor ? selStyle : normStyle);

        // 네트워크 모드 retry 대기 중 표시
        if (m_waitingForPeer) {
            painter.setFont(QFont("Arial", 22, QFont::Bold));
            painter.setPen(QColor(255, 220, 50));
            painter.drawText(QRect(0, height() / 2 + 160, width(), 40), Qt::AlignCenter,
                             "Waiting for opponent...");
        }
    }
}

void MainWindow::gameLoop()
{
    if (gameState == Story) {
        if (storyPage < 4) {
            int elapsed = (int)storyElapsed.elapsed();
            if (elapsed >= 3000) {
                storyPage++;
                storyElapsed.restart();
            }
        } else {
            m_sensor.update();
            aimPos.setX(m_sensor.aimX());
            aimPos.setY(m_sensor.aimY());
            clampAim();
        }
        update();
        return;
    }

    if (gameState == HowToPlay) {
        int elapsed = (int)howToPlayElapsed.elapsed();
        if (elapsed >= howToPlayDurationMs) {
            if (howToPlayPage < 2) {
                howToPlayPage++;
                howToPlayDurationMs = 3000;
                howToPlayElapsed.restart();
            } else {
                showLoading();
            }
        }
        update();
        return;
    }

    if (gameState == Loading) {
        int elapsed = (int)loadingElapsed.elapsed();
        if (elapsed >= 3000) {
            showCountdown();
        }
        update();
        return;
    }

    if (gameState == Countdown) {
        int elapsed = (int)countdownElapsed.elapsed();
        if (elapsed >= 1000) {
            if (countdownPage < 2) {
                countdownPage++;
                countdownElapsed.restart();
            } else {
                resetGame();
                gameState = Playing;
                gameElapsed.start();
                m_audio->playBgm("game");
                updateButtonLayout();
            }
        }
        update();
        return;
    }

    if (gameState != Playing) {
        if (gameState == Calibrating && (calPhase == 0 || calPhase == 1)) {
            m_sensor.update();
            aimPos.setX(m_sensor.aimX());
            aimPos.setY(m_sensor.aimY());
            clampAim();
        }
        fireEffect = false;
        update();
        return;
    }

    // 클라이언트 모드: 에임만 전송
    if (m_network && m_network->role() == NetworkManager::Client) {
        m_sensor.update();
        aimPos.setX(m_sensor.aimX());
        aimPos.setY(m_sensor.aimY());
        clampAim();
        if (m_network->isConnected())
            m_network->sendAim(aimPos.x(), aimPos.y());
        for (int i = activeHits.size() - 1; i >= 0; --i) {
            activeHits[i].framesLeft--;
            if (activeHits[i].framesLeft <= 0)
                activeHits.removeAt(i);
        }
        // Client-side enemyAttack overlay countdown
        if (enemyAttackActive && enemyAttackTimer.elapsed() >= 2000) {
            enemyAttackActive = false;
        }
        fireEffect = false;
        update();
        return;
    }

    // 서버 또는 단독 모드
    int remainingMs = gameDurationMs - (int)gameElapsed.elapsed();
    if (remainingMs <= 0) {
        remainingMs = 0;
        gameState = GameOver;
        gameOverCursor = 0;
        gameOverTimer.restart();
        m_audio->stopBgm();
        updateButtonLayout();
        if (m_network && m_network->role() == NetworkManager::Server) {
            m_network->sendState(buildStateString(0));
        }
    }

    m_sensor.update();
    aimPos.setX(m_sensor.aimX());
    aimPos.setY(m_sensor.aimY());
    clampAim();

    // 타겟 이동
    int margin = 120;
    int minX = margin,     maxX = qMax(minX+1, width()  - margin);
    int minY = 110,        maxY = qMax(minY+1, height() - 120);

    for (Target &t : targets) {
        if (isGroundUnit(t.typeName)) {
            t.dirChangeFrames--;
            if (t.dirChangeFrames <= 0) {
                int spd = qMax(1, qAbs(t.speedX));
                t.speedX = QRandomGenerator::global()->bounded(2) ? spd : -spd;
                t.speedY = QRandomGenerator::global()->bounded(2) ? spd : -spd;
                t.dirChangeFrames = randomDirChangeFrames();
            }
        }

        t.pos.rx() += t.speedX;
        t.pos.ry() += t.speedY;
        if (t.pos.x() < minX) { t.pos.setX(minX); t.speedX = qAbs(t.speedX); }
        if (t.pos.x() > maxX) { t.pos.setX(maxX); t.speedX = -qAbs(t.speedX); }

        if (isGroundUnit(t.typeName)) {
            int groundMinY = height() / 2 - 20;
            int groundMaxY = qMax(groundMinY + 1, height() - 120);
            if (t.pos.y() < groundMinY) { t.pos.setY(groundMinY); t.speedY = qAbs(t.speedY); }
            if (t.pos.y() > groundMaxY) { t.pos.setY(groundMaxY); t.speedY = -qAbs(t.speedY); }
        } else {
            if (t.pos.y() < minY) { t.pos.setY(minY); t.speedY = qAbs(t.speedY); }
            if (t.pos.y() > maxY) { t.pos.setY(maxY); t.speedY = -qAbs(t.speedY); }
        }
    }

    // 서버 멀티플레이어: 클라이언트 발사 + 상태 전송
    if (m_network && m_network->role() == NetworkManager::Server && gameState == Playing) {
        int hitCode = m_pendingHitCode;
        m_pendingHitCode = 0;
        if (m_clientFirePending) {
            m_clientFirePending = false;
            int ch = processHitForPlayer(m_peerAimPos, false);
            if (ch != 0) hitCode = ch;
        }
        m_network->sendState(buildStateString(hitCode));
    }

    // 명중 효과 프레임 감소
    for (int i = activeHits.size() - 1; i >= 0; --i) {
        activeHits[i].framesLeft--;
        if (activeHits[i].framesLeft <= 0)
            activeHits.removeAt(i);
    }

    // --- Blackgatmon logic (서버/단독 모드 전용) ---
    if (!m_network || m_network->role() == NetworkManager::Server) {
        // enemy_attack overlay countdown
        if (enemyAttackActive) {
            if (enemyAttackTimer.elapsed() >= 2000) {
                enemyAttackActive = false;
                m_serverEnemyAttackOnly = false;
                if (!blackgatmonActive) {
                    blackgatmonNextSpawnMs = QRandomGenerator::global()->bounded(5000, 10001);
                    blackgatmonSpawnCooldown.restart();
                }
            }
        }
        // Blackgatmon active: pop-up animation + phase system
        if (blackgatmonActive) {
            int bgSize = 300;
            float targetY = height() - bgSize + 60;

            if (!blackgatmonPopped) {
                if (blackgatmonPopY > targetY)
                    blackgatmonPopY -= 16.0f;
                if (blackgatmonPopY <= targetY) {
                    blackgatmonPopY = targetY;
                    blackgatmonPopped = true;
                    blackgatmonPhase = 0;
                    blackgatmonPhaseTimer.restart();
                }
            } else {
                if (blackgatmonPhase == 0 && blackgatmonPhaseTimer.elapsed() >= 500) {
                    blackgatmonPhase = 1;
                    blackgatmonPhaseTimer.restart();
                } else if (blackgatmonPhase == 1 && blackgatmonPhaseTimer.elapsed() >= 1000) {
                    blackgatmonPhase = 2;
                    blackgatmonPhaseTimer.restart();
                    enemyAttackActive = true;
                    m_serverEnemyAttackOnly = false; // natural phase2 attack: both players show it
                    enemyAttackTimer.restart();
                } else if (blackgatmonPhase == 2) {
                    blackgatmonPopY += 16.0f;
                    if (blackgatmonPopY >= (float)height()) {
                        blackgatmonActive = false;
                        blackgatmonPopped = false;
                        if (!enemyAttackActive) {
                            blackgatmonNextSpawnMs = QRandomGenerator::global()->bounded(5000, 10001);
                            blackgatmonSpawnCooldown.restart();
                        }
                    }
                }
            }
        }
        // Spawn cooldown
        else if (!enemyAttackActive) {
            if (blackgatmonSpawnCooldown.elapsed() >= blackgatmonNextSpawnMs) {
                blackgatmonActive = true;
                blackgatmonSlot = QRandomGenerator::global()->bounded(3);
                blackgatmonPopY = (float)height();
                blackgatmonPopped = false;
                blackgatmonPhase = 0;
            }
        }
    }

    fireEffect = false;
    update();
}

/* ================================================================
   GPIO 버튼 (BCM4) 눌림 처리
   ================================================================ */
void MainWindow::onGpioPressed()
{
    switch (gameState) {
    case Settings:
        if (settingsCursor == 0) {
            settingsBgmVol = (settingsBgmVol >= 100) ? 0 : settingsBgmVol + 25;
            m_audio->setBgmVolume(settingsBgmVol);
        } else if (settingsCursor == 1) {
            settingsSfxVol = (settingsSfxVol >= 100) ? 0 : settingsSfxVol + 25;
            m_audio->setSfxVolume(settingsSfxVol);
        } else if (settingsCursor == 2) {
            m_sensor.rezero();
            aimPos = QPoint(width() / 2, height() / 2);
            gameState = prevState;
            if (prevState == Playing) {
                gameDurationMs = pausedRemainingMs;
                gameElapsed.restart();
            }
            updateButtonLayout();
        } else {
            gameState = prevState;
            if (prevState == Playing) {
                gameDurationMs = pausedRemainingMs;
                gameElapsed.restart();
            }
            updateButtonLayout();
        }
        update();
        break;
    case Menu:
        startGame();
        break;
    case Calibrating:
        fire();
        break;
    case Story:
        if (storyPage == 4) {
            int bw = 280, bh = 70;
            QRect startBtn(width() / 2 - bw / 2, height() - 90, bw, bh);
            if (startBtn.contains(aimPos)) {
                if (m_network) {
                    if (!m_waitingForPeer) {
                        m_waitingForPeer = true;
                        update();
                        m_network->sendReady();
                    }
                } else {
                    showHowToPlay();
                }
            }
        }
        break;
    case HowToPlay:
    case Countdown:
    case Loading:
        /* 진행 중 무시 */
        break;
    case Playing:
        fire();
        break;
    case GameOver:
        if (gameOverTimer.elapsed() < 2000) break;
        if (gameOverCursor == 0)
            retryGame();
        else
            goToMainMenu();
        break;
    }
}

/* SW2 (BCM17): 아래 */
void MainWindow::onSw2Pressed()
{
    if (gameState == Settings) {
        settingsCursor++;
        if (settingsCursor > 3) settingsCursor = 0;
        update();
    } else if (gameState == GameOver) {
        if (gameOverTimer.elapsed() < 2000) return;
        gameOverCursor = (gameOverCursor + 1) % 2;
        update();
    }
}

/* SW3 (BCM18): 위/설정진입 */
void MainWindow::onSw3Pressed()
{
    if (gameState == Settings) {
        settingsCursor--;
        if (settingsCursor < 0) settingsCursor = 3;
        update();
    } else if (gameState == GameOver) {
        if (gameOverTimer.elapsed() < 2000) return;
        gameOverCursor = (gameOverCursor + 1) % 2;
        update();
    } else if (gameState != HowToPlay && gameState != Countdown && gameState != Story && gameState != Loading) {
        prevState = gameState;
        if (gameState == Playing) {
            pausedRemainingMs = gameDurationMs - (int)gameElapsed.elapsed();
            if (pausedRemainingMs < 0) pausedRemainingMs = 0;
        }
        settingsCursor = 0;
        gameState = Settings;
        updateButtonLayout();
        update();
    }
}

// =====================================================================
// setNetworkManager()
// =====================================================================
void MainWindow::setNetworkManager(NetworkManager *nm)
{
    m_network = nm;
    if (!m_network) return;

    m_network->setParent(this);

    connect(m_network, &NetworkManager::syncGo,
            this,      &MainWindow::onSyncGo);
    connect(m_network, &NetworkManager::connectionLost,
            this,      &MainWindow::onConnectionLost);
    connect(m_network, &NetworkManager::aimReceived,
            this,      &MainWindow::onAimReceived);
    connect(m_network, &NetworkManager::fireReceived,
            this,      &MainWindow::onFireReceived);
    connect(m_network, &NetworkManager::stateReceived,
            this,      &MainWindow::onStateReceived);

    m_network->start();

    qDebug() << "[MainWindow] NetworkManager 연결됨. 역할:"
             << (m_network->role() == NetworkManager::Server ? "Server" : "Client");
}

// =====================================================================
// onSyncGo(): 양쪽 모두 READY → GO → 게임 시작
// =====================================================================
void MainWindow::onSyncGo()
{
    qDebug() << "[MainWindow] syncGo 수신";
    m_waitingForPeer = false;
    // GameOver에서 retry한 경우: 바로 카운트다운
    if (gameState == GameOver) {
        showCountdown();
    } else {
        showHowToPlay();
    }
}

// =====================================================================
// onConnectionLost()
// =====================================================================
void MainWindow::onConnectionLost()
{
    qDebug() << "[MainWindow] 연결 끊김 → Menu 복귀";
    m_waitingForPeer = false;
    gameState = Menu;
    updateButtonLayout();
    update();
}

void MainWindow::startGame()
{
    enterCalibration();
}

void MainWindow::showStory()
{
    gameState = Story;
    storyPage = 0;
    storyElapsed.start();
    updateButtonLayout();
    update();
}

void MainWindow::showHowToPlay()
{
    gameState = HowToPlay;
    howToPlayPage = 0;
    howToPlayDurationMs = 3000;
    howToPlayElapsed.start();
    updateButtonLayout();
    update();
}

void MainWindow::showLoading()
{
    gameState = Loading;
    loadingElapsed.start();
    m_audio->stopBgm();
    updateButtonLayout();
    update();
}

void MainWindow::showCountdown()
{
    gameState = Countdown;
    countdownPage = 0;
    m_audio->stopBgm();
    countdownElapsed.start();
    updateButtonLayout();
    update();
}

void MainWindow::enterCalibration()
{
    m_sensorReady = false;
    gameState = Calibrating;
    calPhase = 0;
    aimPos = QPoint(width() / 2, height() / 2);
    calBoxHit[0] = calBoxHit[1] = calBoxHit[2] = false;
    updateButtonLayout();
    repaint();

    m_calibThread = new SensorCalibThread(&m_sensor, this);
    connect(m_calibThread, &SensorCalibThread::calibrationDone,
            this, &MainWindow::onCalibrationDone);
    connect(m_calibThread, &QThread::finished,
            m_calibThread, &QObject::deleteLater);
    m_calibThread->start();
}

void MainWindow::onCalibrationDone()
{
    m_calibThread = nullptr;
    m_sensorReady = true;
    update();
}

void MainWindow::fire()
{
    if (gameState == Calibrating) {
        if (calPhase == 0 && !m_sensorReady)
            return;

        fireEffect = true;
        m_audio->playSfx("fire");

        if (calPhase == 0) {
            centerPos = aimPos;
            aimPos = QPoint(width() / 2, height() / 2);
            calPhase = 1;
        } else {
            int boxW = 70, boxH = 50;
            QRect calBoxes[3] = {
                QRect(width() / 4 - boxW / 2, height() - 180, boxW, boxH),
                QRect(width() / 2 - boxW / 2, 120, boxW, boxH),
                QRect(3 * width() / 4 - boxW / 2, height() - 180, boxW, boxH),
            };
            for (int i = 0; i < 3; i++) {
                if (!calBoxHit[i] && calBoxes[i].contains(aimPos)) {
                    calBoxHit[i] = true;
                    break;
                }
            }
            if (calBoxHit[0] && calBoxHit[1] && calBoxHit[2]) {
                showStory();
            }
        }
        return;
    }
    if (gameState != Playing) return;

    // enemy_attack 오버레이 중에는 발사 불가
    if (enemyAttackActive) return;

    fireEffect = true;
    m_audio->playSfx("fire");

    // 멀티플레이어 분기
    if (m_network) {
        if (m_network->role() == NetworkManager::Server) {
            int hitCode = processHitForPlayer(aimPos, true);
            if (hitCode != 0) {
                m_pendingHitCode = hitCode;
                HitInfo hi;
                hi.pos = aimPos;
                hi.isEnemy = (hitCode == 1 || hitCode == 5);
                hi.framesLeft = hi.isEnemy ? 12 : 6;
                activeHits.append(hi);
            }
        } else {
            if (!enemyAttackActive)
                m_network->sendFire();
        }
        update();
        return;
    }

    // Blackgatmon 히트 체크 (단독 모드)
    if (blackgatmonActive) {
        int bgSize = 300;
        int slotX;
        if (blackgatmonSlot == 0)      slotX = width() / 4 - bgSize / 2;
        else if (blackgatmonSlot == 1)  slotX = width() / 2 - bgSize / 2;
        else                            slotX = 3 * width() / 4 - bgSize / 2;
        QRect bgRect(slotX, (int)blackgatmonPopY, bgSize, bgSize);
        const QImage &mask = (blackgatmonPhase == 2) ? blackgatmon3Mask
                           : (blackgatmonPhase == 1) ? blackgatmon2Mask
                           : blackgatmonMask;
        if (pointHitsOpaquePixel(mask, bgRect, aimPos)) {
            blackgatmonActive = false;
            score += 100;
            HitInfo hi;
            hi.pos = aimPos;
            hi.isEnemy = true;
            hi.framesLeft = 12; // blackgatmon is enemy, keep 12
            activeHits.append(hi);
            m_audio->playSfx("enemy_dead");
            blackgatmonNextSpawnMs = QRandomGenerator::global()->bounded(5000, 10001);
            blackgatmonSpawnCooldown.restart();
        }
    }

    // 단독 모드: 명중 판정
    static const QPoint hitProbeOffsets[] = {
        QPoint(0, 0),
        QPoint(8, 0), QPoint(-8, 0),
        QPoint(0, 8), QPoint(0, -8),
        QPoint(6, 6), QPoint(-6, 6),
        QPoint(6, -6), QPoint(-6, -6)
    };

    for (int i = 0; i < targets.size(); ++i) {
        Target &t = targets[i];
        bool hitTarget = false;
        for (const QPoint &ofs : hitProbeOffsets) {
            if (pointHitsTarget(t, aimPos + ofs)) {
                hitTarget = true;
                break;
            }
        }

        if (hitTarget && !isBlockedByWall(aimPos, aimPos)) {
            score += t.points;
            HitInfo hi;
            hi.pos = aimPos;
            hi.isEnemy = t.isEnemy;
            hi.framesLeft = t.isEnemy ? 12 : 6;
            activeHits.append(hi);
            if (t.isEnemy)
                m_audio->playSfx("enemy_dead");
            else
                m_audio->playSfx("ally_dead");
            if (t.isEnemy)
                targets[i] = spawnEnemy();
            else
                targets[i] = spawnAlly();
        }
    }

    update();
}

void MainWindow::retryGame()
{
    m_waitingForPeer = false;
    resetGame();
    if (m_network) {
        m_network->resetReady();
        // 네트워크 모드 retry: READY 동기화 후 바로 카운트다운
        m_waitingForPeer = true;
        m_network->sendReady();
        update();
    } else {
        showCountdown();
    }
}

void MainWindow::goToMainMenu()
{
    m_waitingForPeer = false;
    if (m_network) m_network->resetReady();
    resetGame();
    gameState = Menu;
    centerPos = QPoint(width() / 2, height() / 2);
    aimPos = centerPos;
    m_audio->playBgm("menu");
    updateButtonLayout();
    update();
}

// =====================================================================
// processHitForPlayer(): 명중 판정 (서버 전용)
// =====================================================================
int MainWindow::processHitForPlayer(QPoint aim, bool isServer)
{
    // Blackgatmon 히트 체크 (멀티플레이어)
    if (blackgatmonActive) {
        int bgSize = 300;
        int slotX;
        if (blackgatmonSlot == 0)      slotX = width() / 4 - bgSize / 2;
        else if (blackgatmonSlot == 1)  slotX = width() / 2 - bgSize / 2;
        else                            slotX = 3 * width() / 4 - bgSize / 2;
        QRect bgRect(slotX, (int)blackgatmonPopY, bgSize, bgSize);
        const QImage &mask = (blackgatmonPhase == 2) ? blackgatmon3Mask
                           : (blackgatmonPhase == 1) ? blackgatmon2Mask
                           : blackgatmonMask;
        if (pointHitsOpaquePixel(mask, bgRect, aim)) {
            blackgatmonActive = false;
            if (isServer) m_serverScore += 100;
            else          m_clientScore += 100;
            m_audio->playSfx("enemy_dead");
            blackgatmonNextSpawnMs = QRandomGenerator::global()->bounded(5000, 10001);
            blackgatmonSpawnCooldown.restart();
            // hitCode 5 = host killed → client gets enemy_attack
            // hitCode 6 = client killed → host gets enemy_attack
            if (isServer) {
                // host killed blackgatmon: host does NOT get enemy_attack, client will
                return 5;
            } else {
                // client killed blackgatmon: host gets enemy_attack (client should NOT)
                enemyAttackActive = true;
                m_serverEnemyAttackOnly = true;
                enemyAttackTimer.restart();
                return 6;
            }
        }
    }

    static const QPoint offsets[] = {
        QPoint(0, 0),   QPoint(8, 0),  QPoint(-8, 0),
        QPoint(0, 8),   QPoint(0, -8), QPoint(6, 6),
        QPoint(-6, 6),  QPoint(6, -6), QPoint(-6, -6)
    };
    for (int i = 0; i < targets.size(); ++i) {
        Target &t = targets[i];
        bool hit = false;
        for (const QPoint &ofs : offsets) {
            if (pointHitsTarget(t, aim + ofs)) { hit = true; break; }
        }
        if (hit && !isBlockedByWall(aim, aim)) {
            bool enemy = t.isEnemy;
            if (isServer) m_serverScore += t.points;
            else          m_clientScore += t.points;
            if (enemy) { m_audio->playSfx("enemy_dead"); targets[i] = spawnEnemy(); }
            else       { m_audio->playSfx("ally_dead");  targets[i] = spawnAlly(); }
            return enemy ? (isServer ? 1 : 3) : (isServer ? 2 : 4);
        }
    }
    return 0;
}

// =====================================================================
// buildStateString(): 서버 → 클라이언트 STATE 문자열 생성
// =====================================================================
QString MainWindow::buildStateString(int hitCode)
{
    int remainMs = qMax(0, gameDurationMs - (int)gameElapsed.elapsed());
    // hitCode 5 = host killed blackgatmon (client gets enemy_attack)
    // hitCode 6 = client killed blackgatmon (host gets enemy_attack)
    QString s = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9")
        .arg(remainMs).arg(m_serverScore).arg(m_clientScore)
        .arg(aimPos.x()).arg(aimPos.y())
        .arg(m_peerAimPos.x()).arg(m_peerAimPos.y())
        .arg(hitCode).arg(targets.size());
    for (const Target &t : targets)
        s += QString(" %1 %2 %3 %4 %5")
            .arg(t.typeName).arg(t.pos.x()).arg(t.pos.y())
            .arg(t.radius).arg(t.isEnemy ? 1 : 0);
    // Blackgatmon state: active slot popY popped phase
    s += QString(" %1 %2 %3 %4 %5")
        .arg(blackgatmonActive ? 1 : 0)
        .arg(blackgatmonSlot)
        .arg((int)blackgatmonPopY)
        .arg(blackgatmonPopped ? 1 : 0)
        .arg(blackgatmonPhase);
    // enemyAttack on client side: only propagate natural phase2 attack (not when caused by client kill)
    s += QString(" %1").arg((enemyAttackActive && !m_serverEnemyAttackOnly) ? 1 : 0);
    return s;
}

// =====================================================================
// parseStateString(): 클라이언트가 서버 STATE 파싱
// =====================================================================
void MainWindow::parseStateString(const QString &s)
{
    QStringList p = s.split(' ');
    int i = 0;
    if (p.size() < 9) return;

    m_remoteRemainMs = p[i++].toInt();
    m_serverScore    = p[i++].toInt();
    m_clientScore    = p[i++].toInt();
    m_peerAimPos.setX(p[i++].toInt());
    m_peerAimPos.setY(p[i++].toInt());
    i++; i++;  // 클라이언트 에임 에코 (로컬 aimPos 사용)
    int hitCode = p[i++].toInt();
    int n       = p[i++].toInt();

    // 명중 사운드
    if      (hitCode == 1 || hitCode == 3 || hitCode == 5 || hitCode == 6) m_audio->playSfx("enemy_dead");
    else if (hitCode == 2 || hitCode == 4) m_audio->playSfx("ally_dead");

    // 클라이언트가 맞힌 경우: 로컬 에임에 효과 표시
    if (hitCode == 3 || hitCode == 4 || hitCode == 6) {
        HitInfo hi;
        hi.pos = aimPos;
        hi.isEnemy = (hitCode == 3 || hitCode == 6);
        hi.framesLeft = hi.isEnemy ? 12 : 6;
        activeHits.append(hi);
    }

    // hitCode 5 = host killed blackgatmon → client gets enemy_attack overlay
    // hitCode 6 = client killed blackgatmon → host gets enemy_attack (handled on server side)
    if (hitCode == 5) {
        enemyAttackActive = true;
        enemyAttackTimer.restart();
    }

    // 타겟 목록 파싱
    m_remoteTargets.clear();
    for (int j = 0; j < n && i + 5 <= p.size(); ++j) {
        Target t;
        t.typeName = p[i++];
        t.pos.setX(p[i++].toInt());
        t.pos.setY(p[i++].toInt());
        t.radius   = p[i++].toInt();
        t.isEnemy  = p[i++].toInt() != 0;
        t.speedX = t.speedY = t.points = t.dirChangeFrames = 0;
        m_remoteTargets.append(t);
    }

    // Blackgatmon state 파싱
    if (i + 6 <= p.size()) {
        blackgatmonActive = p[i++].toInt() != 0;
        blackgatmonSlot   = p[i++].toInt();
        blackgatmonPopY   = (float)p[i++].toInt();
        blackgatmonPopped = p[i++].toInt() != 0;
        blackgatmonPhase  = p[i++].toInt();
        // enemyAttackActive from server (for non-blackgatmon-kill overlay like phase2 natural attack)
        // Only set if not already set by hitCode 5
        bool serverEnemyAttack = p[i++].toInt() != 0;
        // Client shows enemyAttackActive from hitCode 5 (cross-player) or server's natural phase2 attack
        // The server's enemyAttackActive is for the server itself; client gets it from hitCode 5
        // But phase2 natural enemy_attack should also affect client
        // Actually: server enemyAttackActive means blackgatmon did phase2 attack on BOTH players
        // so client should show it too
        if (serverEnemyAttack && !enemyAttackActive) {
            enemyAttackActive = true;
            enemyAttackTimer.restart();
        }
    }

    // 게임 종료 감지
    if (m_remoteRemainMs <= 0 && gameState == Playing) {
        gameState = GameOver;
        gameOverCursor = 0;
        gameOverTimer.restart();
        m_audio->stopBgm();
        updateButtonLayout();
    }
}

void MainWindow::onAimReceived(int x, int y)
{
    m_peerAimPos = QPoint(x, y);
}

void MainWindow::onFireReceived()
{
    m_clientFirePending = true;
}

void MainWindow::onStateReceived(const QString &stateData)
{
    parseStateString(stateData);
    update();
}

void MainWindow::clampAim()
{
    int leftBound = 0;
    int rightBound = width();
    int topBound = 0;
    int bottomBound = height();

    if (aimPos.x() < leftBound) aimPos.setX(leftBound);
    if (aimPos.x() > rightBound) aimPos.setX(rightBound);
    if (aimPos.y() < topBound) aimPos.setY(topBound);
    if (aimPos.y() > bottomBound) aimPos.setY(bottomBound);
}

void MainWindow::resetGame()
{
    score = 0;
    gameDurationMs = 30000;
    fireEffect = false;
    activeHits.clear();
    blackgatmonActive = false;
    enemyAttackActive = false;
    blackgatmonNextSpawnMs = QRandomGenerator::global()->bounded(5000, 10001);
    blackgatmonSpawnCooldown.restart();
    aimPos = centerPos;
    m_serverScore = 0;
    m_clientScore = 0;
    m_clientFirePending = false;
    m_pendingHitCode = 0;
    m_remoteRemainMs = 30000;
    m_remoteTargets.clear();
    resetTargets();
}

