#include "mainwindow.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QFont>
#include <QFontDatabase>
#include <QPixmap>
#include <QLineF>
#include <QtMath>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>

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
        || name == "oogamon" || name == "raremon" || name == "gajimon";
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
      m_serverEnemyAttackOnly(false),
      m_gameMode(Solo),
      m_modeLocked(false),
      m_ranking(new RankingManager("ranking", this)),
      m_menuCursor(0),
      m_rankingTab(0),
      m_rankingSaved(false)
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
    treeRects[0] = QRect(60, 360, 180, 220);
    treeRects[1] = QRect(780, 340, 160, 200);
    bushRects[0] = QRect(744, 230, 200, 140);
    bushRects[1] = QRect(120, 200, 180, 130);
    leafRects[0] = QRect(272, -20, 240, 140);
    leafRects[1] = QRect(500, 30, 200, 130);
    leafRects[2] = QRect(700, 10, 200, 130);

    // 한글 폰트 로드
    QFontDatabase::addApplicationFont("fonts/NanumGothic-Bold.ttf");
    QFontDatabase::addApplicationFont("fonts/Jua-Regular.ttf");
    QFontDatabase::addApplicationFont("fonts/stencil.ttf");

    setupUiButtons();
    resetTargets();

    // ALSA 사운드 초기화
    m_audio = new AlsaPlayer(this);
    m_audio->loadSfx("fire", "sounds/fire.wav", 150);
    m_audio->loadSfx("enemy_dead", "sounds/enemy_dead.wav");
    m_audio->loadSfx("ally_dead", "sounds/ally_dead.wav");
    m_audio->loadSfx("enemy_attack", "sounds/enemy_attack.wav");
    m_audio->loadBgm("menu", "sounds/butterfly.wav");
    m_audio->setBgmVolume(100);
    m_audio->loadBgm("game", "sounds/gamebgm.wav");
    m_audio->playBgm("menu");

    connect(timer, &QTimer::timeout, this, &MainWindow::gameLoop);
    timer->start(30);

    // photos 디렉토리 생성
    QDir().mkpath("photos");

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
        "  background-color: rgba(50, 50, 55, 210);"
        "  color: white;"
        "  border: 1px solid rgba(255,255,255,70);"
        "  border-radius: 14px;"
        "  font-size: 20px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(85, 85, 95, 220);"
        "}";

    QString startStyle =
        "QPushButton {"
        "  color: rgb(255, 240, 200);"
        "  background-color: rgba(185, 45, 12, 230);"
        "  border: 2px solid rgba(255, 240, 200, 200);"
        "  border-radius: 18px;"
        "  padding: 8px 20px;"
        "  font-size: 28px;"
        "  font-weight: 700;"
        "  font-family: 'Stencil';"
        "  letter-spacing: 4px;"
        "}"
        "QPushButton:hover {"
        "  color: rgb(255, 240, 160);"
        "  background-color: rgba(215, 60, 15, 240);"
        "  border: 2px solid rgba(255, 240, 200, 230);"
        "}"
        "QPushButton:pressed {"
        "  color: rgb(255, 210, 100);"
        "  background-color: rgba(120, 20, 5, 245);"
        "  border: 2px solid rgba(255, 240, 200, 170);"
        "  padding-top: 14px;"
        "  padding-bottom: 10px;"
        "}";

    btnRetry->setStyleSheet(commonStyle);
    btnMainMenu->setStyleSheet(commonStyle);
    btnStart->setStyleSheet(startStyle);

    for (QPushButton *btn : {btnStart, btnRetry, btnMainMenu}) {
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setAutoRepeat(false);
    }

    auto *startShadow = new QGraphicsDropShadowEffect(this);
    startShadow->setBlurRadius(24);
    startShadow->setOffset(0, 8);
    startShadow->setColor(QColor(0, 0, 0, 160));
    btnStart->setGraphicsEffect(startShadow);

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
    btnStart->setGeometry(w / 2 - 145, h / 2 + 35, 290, 78);

    bool isGameOver = (gameState == GameOver);

    btnStart->setVisible(false);  // 메뉴는 이제 커서 기반
    btnRetry->setVisible(isGameOver);
    btnMainMenu->setVisible(isGameOver);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    centerPos = QPoint(width() / 2, height() / 2);

    treeRects[0] = QRect(90, height() / 2 + 40, 180, 220);
    treeRects[1] = QRect(width() - 220, height() - 210, 160, 200);
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
        {
            QFont titleFont("Stencil", 60, QFont::Bold);
            titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 2);

            QString title = "FINAL DEFENSE";
            QRect titleRect = rect().adjusted(0, -80, 0, 0);

            QPainterPath path;
            path.addText(0, 0, titleFont, title);

            QRectF br = path.boundingRect();
            qreal x = titleRect.center().x() - br.width() / 2.0;
            qreal y = titleRect.center().y() + br.height() / 2.0;

            QTransform trans;
            trans.translate(x, y);
            QPainterPath titlePath = trans.map(path);

            // 1) 진한 그림자 (2중)
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 120));
            painter.drawPath(titlePath.translated(6, 6));
            painter.setBrush(QColor(0, 30, 0, 100));
            painter.drawPath(titlePath.translated(3, 3));

            // 2) 글자 내부 그라디언트 (군사/디펜스 테마)
            QLinearGradient grad(titleRect.topLeft(), titleRect.bottomLeft());
            grad.setColorAt(0.0,  QColor(255, 230, 120));   // 밝은 노랑
            grad.setColorAt(0.35, QColor(255, 120,  40));   // 주황
            grad.setColorAt(0.7,  QColor(200,  30,  10));   // 진한 빨강
            grad.setColorAt(1.0,  QColor(120,   0,   0));   // 다크 레드

            painter.setBrush(grad);
            painter.setPen(QPen(QColor(30, 0, 0), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(titlePath);

            // 3) 밝은 하이라이트 테두리
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(255, 240, 200), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(titlePath);
        }

        // 메뉴 항목: GAME START, PHOTO, RANKING
        QFont menuFont("Arial", 22, QFont::Bold);
        painter.setFont(menuFont);
        int startY = height() / 2 - 50;
        int lineH = 60;
        QString menuItems[] = { "GAME START", "PHOTO", "RANKING" };
        int menuCount = 3;

        for (int i = 0; i < menuCount; i++) {
            QRect itemRect(width() / 2 - 150, startY + i * lineH, 300, 50);
            if (i == m_menuCursor) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(60, 160, 60, 180));
                painter.drawRoundedRect(itemRect, 12, 12);
                painter.setPen(QColor(100, 255, 100));
                painter.drawText(itemRect, Qt::AlignCenter, QString("> %1 <").arg(menuItems[i]));
            } else {
                painter.setPen(QColor(200, 200, 200));
                painter.setBrush(Qt::NoBrush);
                painter.drawText(itemRect, Qt::AlignCenter, menuItems[i]);
            }
        }

        {
            painter.setFont(QFont("Nanum Gothic", 20, QFont::Normal));
            QRect hintRect(0, height() - 160, width(), 60);
            QString hintText = "게임 시작 후 총을 들고 움직이지 마세요";
            painter.setPen(QColor(255, 240, 200, 180));
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++)
                    if (dx || dy)
                        painter.drawText(hintRect.adjusted(dx, dy, dx, dy), Qt::AlignCenter, hintText);
            painter.setPen(QColor(30, 0, 0));
            painter.drawText(hintRect, Qt::AlignCenter, hintText);
        }
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

    // ── 랭킹 화면 ──
    if (gameState == Ranking) {
        if (!backgroundPixmap.isNull())
            painter.drawPixmap(rect(), backgroundPixmap);
        painter.fillRect(rect(), QColor(0, 0, 0, 180));

        painter.setFont(bigFont);
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, 20, width(), 50), Qt::AlignCenter, "RANKING");

        // 탭 헤더: Single | Cooperative (Competitive 제거)
        QString tabs[] = { "SINGLE", "COOPERATIVE" };
        int tabCount = 2;
        int tabW = 320, tabH = 40;
        int tabStartX = (width() - tabW * tabCount - 10) / 2;
        painter.setFont(QFont("Arial", 16, QFont::Bold));
        for (int i = 0; i < tabCount; i++) {
            QRect tabRect(tabStartX + i * (tabW + 10), 80, tabW, tabH);
            if (i == m_rankingTab) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(60, 160, 60, 200));
                painter.drawRoundedRect(tabRect, 8, 8);
                painter.setPen(QColor(100, 255, 100));
            } else {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(80, 80, 80, 150));
                painter.drawRoundedRect(tabRect, 8, 8);
                painter.setPen(QColor(180, 180, 180));
            }
            painter.drawText(tabRect, Qt::AlignCenter, tabs[i]);
        }

        // 랭킹 항목 표시
        // m_rankingTab: 0=Single, 1=Cooperative
        RankingManager::Mode mode = (m_rankingTab == 0) ? RankingManager::Single
                                                         : RankingManager::Cooperative;
        QVector<RankingManager::Entry> entries;
        if (mode == RankingManager::Cooperative
            && m_network && m_network->role() == NetworkManager::Client) {
            // 클라이언트: 서버에서 받은 캐시 사용
            entries = m_coopRankCache;
        } else {
            entries = m_ranking->getTop3(mode);
        }

        int entryStartY = 150;
        int entryH = 120;
        QString medals[] = { "1st", "2nd", "3rd" };
        QColor medalColors[] = { QColor(255, 215, 0), QColor(192, 192, 192), QColor(205, 127, 50) };

        for (int i = 0; i < 3; i++) {
            QRect entryRect(width() / 2 - 350, entryStartY + i * entryH, 700, entryH - 10);

            // 배경
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(40, 40, 40, 180));
            painter.drawRoundedRect(entryRect, 10, 10);

            // 순위 메달
            painter.setFont(QFont("Arial", 28, QFont::Bold));
            painter.setPen(medalColors[i]);
            painter.drawText(QRect(entryRect.x() + 20, entryRect.y(), 80, entryRect.height()),
                             Qt::AlignVCenter | Qt::AlignLeft, medals[i]);

            // 사진 표시
            QRect photoRect(entryRect.x() + 110, entryRect.y() + 10, 80, 80);
            if (i < entries.size()) {
                if (mode == RankingManager::Cooperative) {
                    // 협동 모드: 사진 2개 (좌우)
                    QRect photo1(entryRect.x() + 110, entryRect.y() + 10, 40, 80);
                    QRect photo2(entryRect.x() + 152, entryRect.y() + 10, 40, 80);
                    QPixmap pm1, pm2;
                    if (!entries[i].photoPath.isEmpty() && pm1.load(entries[i].photoPath)) {
                        painter.drawPixmap(photo1, pm1.scaled(photo1.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                    } else {
                        painter.setPen(QPen(QColor(100, 100, 100), 1));
                        painter.setBrush(QColor(60, 60, 60, 150));
                        painter.drawRect(photo1);
                        painter.setFont(QFont("Arial", 8));
                        painter.setPen(QColor(120, 120, 120));
                        painter.drawText(photo1, Qt::AlignCenter, "P1");
                    }
                    if (!entries[i].photoPath2.isEmpty() && pm2.load(entries[i].photoPath2)) {
                        painter.drawPixmap(photo2, pm2.scaled(photo2.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                    } else {
                        painter.setPen(QPen(QColor(100, 100, 100), 1));
                        painter.setBrush(QColor(60, 60, 60, 150));
                        painter.drawRect(photo2);
                        painter.setFont(QFont("Arial", 8));
                        painter.setPen(QColor(120, 120, 120));
                        painter.drawText(photo2, Qt::AlignCenter, "P2");
                    }
                } else {
                    QPixmap pm;
                    if (!entries[i].photoPath.isEmpty() && pm.load(entries[i].photoPath)) {
                        painter.setPen(QPen(QColor(200, 200, 200), 2));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawRect(photoRect);
                        painter.drawPixmap(photoRect, pm.scaled(photoRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                    } else {
                        painter.setPen(QPen(QColor(100, 100, 100), 1));
                        painter.setBrush(QColor(60, 60, 60, 150));
                        painter.drawRect(photoRect);
                        painter.setFont(QFont("Arial", 10));
                        painter.setPen(QColor(120, 120, 120));
                        painter.drawText(photoRect, Qt::AlignCenter, "PHOTO");
                    }
                }
            } else {
                painter.setPen(QPen(QColor(100, 100, 100), 1));
                painter.setBrush(QColor(60, 60, 60, 150));
                painter.drawRect(photoRect);
            }

            if (i < entries.size()) {
                // 점수
                painter.setFont(QFont("Arial", 32, QFont::Bold));
                painter.setPen(Qt::white);
                painter.drawText(QRect(entryRect.x() + 210, entryRect.y(), 300, entryRect.height()),
                                 Qt::AlignVCenter | Qt::AlignLeft,
                                 QString::number(entries[i].score));
            } else {
                painter.setFont(QFont("Arial", 20));
                painter.setPen(QColor(100, 100, 100));
                painter.drawText(QRect(entryRect.x() + 210, entryRect.y(), 300, entryRect.height()),
                                 Qt::AlignVCenter | Qt::AlignLeft, "---");
            }
        }

        painter.setFont(QFont("Arial", 14));
        drawOutlinedText(QRect(0, height() - 50, width(), 40), Qt::AlignCenter,
                         "SW2/SW3: CHANGE TAB  |  FIRE: BACK");
        return;
    }

    // ── 카메라 촬영 화면 ──
    if (gameState == TakingPhoto) {
        painter.fillRect(rect(), Qt::black);

        if (!m_cameraPreview.isNull()) {
            QPixmap scaled = m_cameraPreview.scaled(
                width(), height() - 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            int px = (width() - scaled.width()) / 2;
            int py = (height() - 60 - scaled.height()) / 2;
            painter.drawPixmap(px, py, scaled);
        } else {
            painter.setFont(QFont("Arial", 24, QFont::Bold));
            painter.setPen(QColor(150, 150, 150));
            painter.drawText(QRect(0, 0, width(), height() - 60), Qt::AlignCenter,
                             "Camera loading...");
        }

        painter.setFont(QFont("Arial", 16, QFont::Bold));
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, height() - 50, width(), 40), Qt::AlignCenter,
                         "Press FIRE to take a photo  |  SW3: BACK");
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
            int bw = 380, bh = 78;
            QRect startBtn(width() / 2 - bw / 2, height() - 100, bw, bh);
            painter.setPen(QPen(QColor(255, 240, 200, 200), 2));
            painter.setBrush(QColor(185, 45, 12, 255));
            painter.drawRoundedRect(startBtn, 18, 18);
            painter.setPen(QColor(255, 240, 200));
            QFont storyStartFont("Stencil", 22, QFont::Bold);
            storyStartFont.setLetterSpacing(QFont::AbsoluteSpacing, 4);
            painter.setFont(storyStartFont);
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
        {
            QFont calTitleFont("Stencil", 48, QFont::Bold);
            calTitleFont.setLetterSpacing(QFont::AbsoluteSpacing, 2);
            QPainterPath calPath;
            calPath.addText(0, 0, calTitleFont, "CALIBRATION");
            QRectF cbr = calPath.boundingRect();
            qreal cx = width() / 2.0 - cbr.width() / 2.0;
            qreal cy = 80.0 + cbr.height();   // 살짝 위쪽
            QTransform ct;
            ct.translate(cx, cy);
            QPainterPath calTitlePath = ct.map(calPath);

            // 그림자
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 120));
            painter.drawPath(calTitlePath.translated(5, 5));
            painter.setBrush(QColor(0, 30, 0, 100));
            painter.drawPath(calTitlePath.translated(3, 3));

            // 단색 채우기
            painter.setBrush(QColor(255, 240, 200));
            painter.setPen(QPen(QColor(30, 0, 0), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(calTitlePath);
        }

        if (calPhase == 0) {
            painter.setFont(subFont);
            if (!m_sensorReady) {
                painter.setFont(QFont("Nanum Gothic", 20, QFont::Normal));
                {
                    QRect htRect = rect().adjusted(0, 200, 0, 0);
                    QString htText = "센서 보정 중입니다. 사과를 조준하고 가만히 계세요...";
                    painter.setPen(QColor(255, 240, 200, 180));
                    for (int dx = -1; dx <= 1; dx++)
                        for (int dy = -1; dy <= 1; dy++)
                            if (dx || dy)
                                painter.drawText(htRect.adjusted(dx, dy, dx, dy), Qt::AlignCenter, htText);
                    painter.setPen(QColor(30, 0, 0));
                    painter.drawText(htRect, Qt::AlignCenter, htText);
                }
                painter.setFont(subFont);
            } else {
                painter.setFont(QFont("Nanum Gothic", 20, QFont::Normal));
                {
                    QRect htRect = rect().adjusted(0, 200, 0, 0);
                    QString htText = "사과를 쏘세요!";
                    painter.setPen(QColor(255, 240, 200, 180));
                    for (int dx = -1; dx <= 1; dx++)
                        for (int dy = -1; dy <= 1; dy++)
                            if (dx || dy)
                                painter.drawText(htRect.adjusted(dx, dy, dx, dy), Qt::AlignCenter, htText);
                    painter.setPen(QColor(30, 0, 0));
                    painter.drawText(htRect, Qt::AlignCenter, htText);
                }
                painter.setFont(subFont);
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
            painter.setFont(QFont("Nanum Gothic", 22, QFont::Normal));
            drawOutlinedText(rect().adjusted(0, 60, 0, 0), Qt::AlignCenter,
                             "3개의 사과를 쏘세요!");
            painter.setFont(subFont);

            int boxW = 70, boxH = 50;
            QRect calBoxes[3] = {
                QRect(width() / 4 - boxW / 2, height() - 180, boxW, boxH),
                QRect(width() / 2 - boxW / 2, 200, boxW, boxH),
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

    if (gameState == ModeSelect) {
        if (!backgroundPixmap.isNull())
            painter.drawPixmap(rect(), backgroundPixmap);
        painter.fillRect(rect(), QColor(0, 0, 0, 130));

        painter.setFont(QFont("Arial", 30, QFont::Bold));
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, 30, width(), 60), Qt::AlignCenter, "SELECT GAME MODE");

        int bw = 300, bh = 90;
        QRect coopBtn(width() / 4 - bw / 2, height() / 2 - bh / 2, bw, bh);
        QRect compBtn(3 * width() / 4 - bw / 2, height() / 2 - bh / 2, bw, bh);

        // Cooperative 버튼
        painter.setPen(QPen(QColor(80, 200, 80), 3));
        painter.setBrush(QColor(30, 130, 30, 200));
        painter.drawRoundedRect(coopBtn, 14, 14);
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 20, QFont::Bold));
        painter.drawText(coopBtn, Qt::AlignCenter, "Cooperative\nMode");

        // Competition 버튼
        painter.setPen(QPen(QColor(200, 80, 80), 3));
        painter.setBrush(QColor(130, 30, 30, 200));
        painter.drawRoundedRect(compBtn, 14, 14);
        painter.setPen(Qt::white);
        painter.drawText(compBtn, Qt::AlignCenter, "Competition\nMode");

        // 설명 텍스트
        painter.setFont(QFont("Arial", 13));
        painter.setPen(QColor(180, 255, 180));
        drawOutlinedText(QRect(width() / 4 - 150, height() / 2 + 55, 300, 40),
                         Qt::AlignCenter, "Team up! Share scores.");
        painter.setPen(QColor(255, 180, 180));
        drawOutlinedText(QRect(3 * width() / 4 - 150, height() / 2 + 55, 300, 40),
                         Qt::AlignCenter, "Compete for high score!");

        if (m_waitingForPeer) {
            painter.setFont(QFont("Arial", 20, QFont::Bold));
            painter.setPen(QColor(255, 220, 50));
            painter.drawText(QRect(0, height() - 80, width(), 40), Qt::AlignCenter,
                             "Waiting for opponent...");
        }

        // 로컬 에임
        QColor modeLocalAimColor = (m_network && m_network->role() == NetworkManager::Client)
            ? QColor(100, 200, 255) : Qt::green;
        painter.setPen(QPen(modeLocalAimColor, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(aimPos, aimRadius, aimRadius);
        painter.drawLine(aimPos.x() - 25, aimPos.y(), aimPos.x() + 25, aimPos.y());
        painter.drawLine(aimPos.x(), aimPos.y() - 25, aimPos.x(), aimPos.y() + 25);

        // 상대방 에임
        if (m_network) {
            QColor modePeerColor = (m_network->role() == NetworkManager::Client)
                ? Qt::green : QColor(100, 200, 255);
            painter.setPen(QPen(modePeerColor, 3));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(m_peerAimPos, aimRadius, aimRadius);
            painter.drawLine(m_peerAimPos.x() - 25, m_peerAimPos.y(), m_peerAimPos.x() + 25, m_peerAimPos.y());
            painter.drawLine(m_peerAimPos.x(), m_peerAimPos.y() - 25, m_peerAimPos.x(), m_peerAimPos.y() + 25);
        }
        return;
    }

    // --- Playing / GameOver ---

    // 상단 정보
    painter.setFont(infoFont);
    if (m_network) {
        if (m_gameMode == Cooperative) {
            painter.setPen(QColor(100, 255, 100));
            painter.drawText(20, 35, QString("TEAM: %1").arg(m_serverScore + m_clientScore));
        } else {
            painter.setPen(Qt::green);
            painter.drawText(20, 35, QString("Player 1: %1").arg(m_serverScore));
            painter.setPen(QColor(100, 200, 255));
            painter.drawText(20, 65, QString("Player 2: %1").arg(m_clientScore));
        }
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

    // 로컬 에임 (Blackgatmon보다 앞, enemy_attack보다 뒤)
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

    // enemy_attack 전체 화면 오버레이
    if (enemyAttackActive && !enemyAttackPixmap.isNull()) {
        painter.drawPixmap(rect(), enemyAttackPixmap);
    }

    // 게임 종료 화면
    if (gameState == GameOver) {
        painter.fillRect(rect(), QColor(0, 0, 0, 180));

        // 랭킹 저장 (1회만)
        if (!m_rankingSaved) {
            m_rankingSaved = true;
            if (m_network) {
                if (m_gameMode == Cooperative) {
                    // 서버만 랭킹 저장 (host: 자기 사진 + client에서 받은 사진)
                    if (m_network->role() == NetworkManager::Server) {
                        int teamScore = m_serverScore + m_clientScore;
                        m_ranking->addScore(RankingManager::Cooperative, teamScore,
                                            m_playerPhotoPath, m_peerPhotoPath);
                        // 클라이언트에게 coop 랭킹 데이터 전송
                        QVector<RankingManager::Entry> top3 = m_ranking->getTop3(RankingManager::Cooperative);
                        QJsonArray arr;
                        for (const auto &e : top3) {
                            QJsonObject obj;
                            obj["score"]     = e.score;
                            obj["photo"]     = e.photoPath;
                            obj["photo2"]    = e.photoPath2;
                            obj["timestamp"] = (double)e.timestamp;
                            arr.append(obj);
                        }
                        m_network->sendCoopRanking(QString::fromUtf8(
                            QJsonDocument(arr).toJson(QJsonDocument::Compact)));
                    }
                }
                // Competitive: 랭킹 없음
            } else {
                m_ranking->addScore(RankingManager::Single, score, m_playerPhotoPath);
            }
        }

        // 점수 표시
        if (m_network) {
            if (m_gameMode == Cooperative) {
                int teamScore = m_serverScore + m_clientScore;
                painter.setFont(QFont("Arial", 32, QFont::Bold));
                painter.setPen(QColor(100, 255, 100));
                painter.drawText(QRect(0, 60, width(), 50), Qt::AlignCenter, "MISSION COMPLETE!");
                painter.setFont(QFont("Arial", 26, QFont::Bold));
                painter.setPen(QColor(255, 220, 50));
                painter.drawText(QRect(0, 115, width(), 45), Qt::AlignCenter,
                                 QString("TEAM SCORE: %1").arg(teamScore));
            } else {
                QString winText;
                QColor  winColor;
                QString winnerPhotoPath;
                if      (m_serverScore > m_clientScore) {
                    winText = "Player 1 WINS!";  winColor = Qt::green;
                    winnerPhotoPath = (m_network->role() == NetworkManager::Server)
                        ? m_playerPhotoPath : m_peerPhotoPath;
                }
                else if (m_clientScore > m_serverScore) {
                    winText = "Player 2 WINS!"; winColor = QColor(100, 200, 255);
                    winnerPhotoPath = (m_network->role() == NetworkManager::Client)
                        ? m_playerPhotoPath : m_peerPhotoPath;
                }
                else { winText = "DRAW!"; winColor = Qt::yellow; }
                painter.setFont(QFont("Arial", 32, QFont::Bold));
                painter.setPen(winColor);
                painter.drawText(QRect(0, 60, width(), 50), Qt::AlignCenter, winText);
                painter.setFont(QFont("Arial", 24, QFont::Bold));
                painter.setPen(Qt::green);
                painter.drawText(QRect(0, 115, width(), 45), Qt::AlignCenter,
                                 QString("Player 1: %1").arg(m_serverScore));
                painter.setPen(QColor(100, 200, 255));
                painter.drawText(QRect(0, 160, width(), 45), Qt::AlignCenter,
                                 QString("Player 2: %1").arg(m_clientScore));

                // 승자 사진 크게 표시
                if (!winnerPhotoPath.isEmpty()) {
                    QPixmap winPm;
                    if (winPm.load(winnerPhotoPath)) {
                        int photoH = height() - 190;
                        if (photoH > 300) photoH = 300;
                        int photoW = (int)(photoH * 4.0 / 3.0);
                        QRect photoRect(width() / 2 - photoW / 2, 210, photoW, photoH);
                        painter.drawPixmap(photoRect, winPm.scaled(photoRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                        painter.setPen(QPen(winColor, 3));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawRect(photoRect);
                    }
                }
            }
        } else {
            painter.setFont(QFont("Arial", 28, QFont::Bold));
            painter.setPen(QColor(255, 220, 50));
            painter.drawText(QRect(0, 60, width(), 50), Qt::AlignCenter,
                             QString("Final Score: %1").arg(score));
        }

        // 랭킹 표시 (Single / Cooperative만 — Competitive는 승자 사진으로 대체)
        bool showRanking = true;
        if (m_network && m_gameMode != Cooperative) showRanking = false;

        if (showRanking) {
            QVector<RankingManager::Entry> top3;
            if (m_network && m_gameMode == Cooperative) {
                // 서버: 로컬 랭킹, 클라이언트: 수신된 캐시
                if (m_network->role() == NetworkManager::Server)
                    top3 = m_ranking->getTop3(RankingManager::Cooperative);
                else
                    top3 = m_coopRankCache;
            } else {
                top3 = m_ranking->getTop3(RankingManager::Single);
            }

            int rankStartY = 215;
            painter.setFont(QFont("Arial", 16, QFont::Bold));
            painter.setPen(QColor(200, 200, 200));
            painter.drawText(QRect(0, rankStartY - 25, width(), 20), Qt::AlignCenter, "- TOP 3 -");

            QString medals[] = { "1st", "2nd", "3rd" };
            QColor medalColors[] = { QColor(255, 215, 0), QColor(192, 192, 192), QColor(205, 127, 50) };
            int entryH = 45;

            for (int i = 0; i < 3; i++) {
                int y = rankStartY + i * entryH;
                painter.setFont(QFont("Arial", 16, QFont::Bold));
                painter.setPen(medalColors[i]);
                painter.drawText(QRect(width() / 2 - 160, y, 60, entryH), Qt::AlignVCenter | Qt::AlignRight, medals[i]);

                // 사진 썸네일
                if (i < top3.size() && m_network && m_gameMode == Cooperative) {
                    // 협동 모드: 사진 2개 (좌우)
                    QRect photo1(width() / 2 - 90, y + 4, 18, 36);
                    QRect photo2(width() / 2 - 72, y + 4, 18, 36);
                    QPixmap pm1, pm2;
                    if (!top3[i].photoPath.isEmpty() && pm1.load(top3[i].photoPath)) {
                        painter.drawPixmap(photo1, pm1.scaled(photo1.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                        painter.setPen(QPen(medalColors[i], 1));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawRect(photo1);
                    } else {
                        painter.setPen(QPen(QColor(100, 100, 100), 1));
                        painter.setBrush(QColor(60, 60, 60, 150));
                        painter.drawRect(photo1);
                        painter.setFont(QFont("Arial", 6));
                        painter.setPen(QColor(120, 120, 120));
                        painter.drawText(photo1, Qt::AlignCenter, "P1");
                    }
                    if (!top3[i].photoPath2.isEmpty() && pm2.load(top3[i].photoPath2)) {
                        painter.drawPixmap(photo2, pm2.scaled(photo2.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                        painter.setPen(QPen(medalColors[i], 1));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawRect(photo2);
                    } else {
                        painter.setPen(QPen(QColor(100, 100, 100), 1));
                        painter.setBrush(QColor(60, 60, 60, 150));
                        painter.drawRect(photo2);
                        painter.setFont(QFont("Arial", 6));
                        painter.setPen(QColor(120, 120, 120));
                        painter.drawText(photo2, Qt::AlignCenter, "P2");
                    }
                } else {
                    QRect thumbRect(width() / 2 - 90, y + 4, 36, 36);
                    if (i < top3.size() && !top3[i].photoPath.isEmpty()) {
                        QPixmap pm;
                        if (pm.load(top3[i].photoPath)) {
                            painter.drawPixmap(thumbRect, pm.scaled(thumbRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                            painter.setPen(QPen(medalColors[i], 2));
                            painter.setBrush(Qt::NoBrush);
                            painter.drawRect(thumbRect);
                        } else {
                            painter.setPen(QPen(QColor(100, 100, 100), 1));
                            painter.setBrush(QColor(60, 60, 60, 150));
                            painter.drawRect(thumbRect);
                        }
                    } else {
                        painter.setPen(QPen(QColor(80, 80, 80), 1));
                        painter.setBrush(QColor(50, 50, 50, 120));
                        painter.drawRect(thumbRect);
                    }
                }

                if (i < top3.size()) {
                    painter.setFont(QFont("Arial", 16, QFont::Bold));
                    painter.setPen(Qt::white);
                    painter.drawText(QRect(width() / 2 - 40, y, 160, entryH),
                                     Qt::AlignVCenter | Qt::AlignLeft,
                                     QString::number(top3[i].score));
                } else {
                    painter.setPen(QColor(100, 100, 100));
                    painter.drawText(QRect(width() / 2 - 40, y, 160, entryH),
                                     Qt::AlignVCenter | Qt::AlignLeft, "---");
                }
            }
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
            m_sensor.update();  // Story 1~4: 센서 추적 유지 (내려놓았다 들면 상쇄)
            aimPos.setX(m_sensor.aimX());
            aimPos.setY(m_sensor.aimY());
            clampAim();
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

    if (gameState == ModeSelect) {
        m_sensor.update();
        aimPos.setX(m_sensor.aimX());
        aimPos.setY(m_sensor.aimY());
        clampAim();
        if (m_network && m_network->isConnected()) {
            // 클라이언트: 에임을 서버에 전송
            if (m_network->role() == NetworkManager::Client)
                m_network->sendAim(aimPos.x(), aimPos.y());
            // 서버: 에임을 클라이언트에 전송 (SAIM)
            else
                m_network->sendServerAim(aimPos.x(), aimPos.y());
        }
        update();
        return;
    }

    // V4L2 카메라 미리보기 갱신
    if (gameState == TakingPhoto) {
        QImage frame = m_camera.captureFrame();
        if (!frame.isNull())
            m_cameraPreview = QPixmap::fromImage(frame);
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
                    m_audio->playSfx("enemy_attack");
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
            m_audio->playSfx("fire");
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
        if (m_menuCursor == 0) {
            startGame();
        } else if (m_menuCursor == 1) {
            // 카메라 촬영 진입
            m_cameraPreview = QPixmap();
            gameState = TakingPhoto;
            updateButtonLayout();
            m_camera.open();
            m_camera.startStreaming();
            update();
        } else if (m_menuCursor == 2) {
            m_rankingTab = 0;
            gameState = Ranking;
            updateButtonLayout();
            update();
        }
        break;
    case Calibrating:
        fire();
        break;
    case Story:
        if (storyPage == 4) {
            int bw = 380, bh = 78;
            QRect startBtn(width() / 2 - bw / 2, height() - 100, bw, bh);
            if (startBtn.contains(aimPos)) {
                if (m_network) {
                    showModeSelect();
                } else {
                    showHowToPlay();
                }
            }
        }
        break;
    case ModeSelect: {
        if (m_waitingForPeer) break; // 이미 선택 후 서버 확인 대기 중
        int bw = 300, bh = 90;
        QRect coopBtn(width() / 4 - bw / 2, height() / 2 - bh / 2, bw, bh);
        QRect compBtn(3 * width() / 4 - bw / 2, height() / 2 - bh / 2, bw, bh);
        bool hitCoop = coopBtn.contains(aimPos);
        bool hitComp = compBtn.contains(aimPos);
        if (!hitCoop && !hitComp) break;
        if (m_network) {
            if (m_network->role() == NetworkManager::Server) {
                if (m_modeLocked) break;
                m_modeLocked = true;
                m_gameMode = hitCoop ? Cooperative : Competition;
                m_network->sendMode(hitCoop); // 클라이언트에게 확정된 모드 전송
                showHowToPlay();
            } else {
                // 클라이언트: 선택을 서버에 전송 후 서버 확인 대기
                m_waitingForPeer = true;
                m_network->sendMode(hitCoop);
                update();
            }
        }
        break;
    }
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
    case Ranking:
        gameState = Menu;
        updateButtonLayout();
        update();
        break;
    case TakingPhoto:
        // 발사 버튼으로 촬영
        m_playerPhotoPath = QString("photos/player_%1.jpg")
            .arg(QDateTime::currentSecsSinceEpoch());
        m_camera.saveFrame(m_playerPhotoPath);
        m_camera.stopStreaming();
        m_camera.close();
        // 클라이언트: 사진을 서버로 전송 / 서버: 사진을 클라이언트로 전송
        if (m_network)
            m_network->sendPhoto(m_playerPhotoPath);
        gameState = Menu;
        updateButtonLayout();
        update();
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
    } else if (gameState == Menu) {
        m_menuCursor = (m_menuCursor + 1) % 3;
        update();
    } else if (gameState == Ranking) {
        m_rankingTab = (m_rankingTab + 1) % 2;
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
    } else if (gameState == Menu) {
        m_menuCursor = (m_menuCursor - 1 + 3) % 3;
        update();
    } else if (gameState == Ranking) {
        m_rankingTab = (m_rankingTab - 1 + 2) % 2;
        update();
    } else if (gameState == TakingPhoto) {
        // 뒤로가기
        m_camera.stopStreaming();
        m_camera.close();
        gameState = Menu;
        updateButtonLayout();
        update();
    } else if (gameState != HowToPlay && gameState != Countdown && gameState != Story && gameState != Loading && gameState != ModeSelect && gameState != TakingPhoto) {
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
    connect(m_network, &NetworkManager::modeReceived,
            this,      &MainWindow::onModeReceived);

    // 상대 사진 수신 처리 (서버←클라이언트, 클라이언트←서버 양방향)
    connect(m_network, &NetworkManager::photoReceived,
            this, [this](const QByteArray &jpegData) {
        QString prefix = (m_network->role() == NetworkManager::Server) ? "client" : "server";
        m_peerPhotoPath = QString("photos/%1_%2.jpg")
            .arg(prefix).arg(QDateTime::currentSecsSinceEpoch());
        QDir().mkpath("photos");
        QFile f(m_peerPhotoPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(jpegData);
            f.close();
            qDebug() << "[MainWindow] 상대 사진 저장:" << m_peerPhotoPath;
        }
    });

    // 클라이언트: 협동 랭킹 데이터 수신 처리
    connect(m_network, &NetworkManager::coopRankingReceived,
            this, [this](const QString &jsonData) {
        m_coopRankCache.clear();
        QJsonDocument doc = QJsonDocument::fromJson(jsonData.toUtf8());
        QJsonArray arr = doc.array();
        for (const QJsonValue &v : arr) {
            QJsonObject obj = v.toObject();
            RankingManager::Entry e;
            e.score      = obj["score"].toInt();
            e.photoPath  = obj["photo"].toString();
            e.photoPath2 = obj["photo2"].toString();
            e.timestamp  = (qint64)obj["timestamp"].toDouble();
            m_coopRankCache.append(e);
        }
        qDebug() << "[MainWindow] 협동 랭킹 수신:" << m_coopRankCache.size() << "entries";
        update();
    });

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
    // 멀티플레이어: 항상 모드 선택 화면으로 이동 (게임 시작 / retry 모두)
    showModeSelect();
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

void MainWindow::showModeSelect()
{
    if (!m_network) {
        showHowToPlay();
        return;
    }
    m_modeLocked = false;
    m_waitingForPeer = false;
    m_gameMode = Solo;
    m_network->resetModeLock();
    gameState = ModeSelect;
    updateButtonLayout();
    update();
}

void MainWindow::onModeReceived(bool cooperative)
{
    if (m_network && m_network->role() == NetworkManager::Server) {
        // 서버: 클라이언트가 먼저 선택한 경우 (선착순 확정)
        if (m_modeLocked) return;
        m_modeLocked = true;
        m_gameMode = cooperative ? Cooperative : Competition;
        m_network->sendMode(cooperative); // 확정된 모드를 클라이언트에게 에코
        showHowToPlay();
    } else {
        // 클라이언트: 서버로부터 확정된 모드 수신
        m_gameMode = cooperative ? Cooperative : Competition;
        m_waitingForPeer = false;
        showHowToPlay();
    }
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
                QRect(width() / 2 - boxW / 2, 200, boxW, boxH),
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
                hi.framesLeft = 6;
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
            hi.framesLeft = 6;
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
            hi.framesLeft = 6;
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
        m_network->resetModeLock();
        // 네트워크 모드 retry: 모드 선택 화면으로 복귀
        showModeSelect();
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
                // Competition: host 쪽에는 공격 없음, client에 enemy_attack (hitCode 5가 클라이언트에서 처리)
                // Cooperative: 아무도 공격받지 않음 (hitCode 5는 클라이언트에서 조건부로 처리)
                return 5;
            } else {
                // Competition: host gets enemy_attack (client should NOT)
                // Cooperative: 아무도 공격받지 않음
                if (m_gameMode != Cooperative) {
                    enemyAttackActive = true;
                    m_serverEnemyAttackOnly = true;
                    enemyAttackTimer.restart();
                    m_audio->playSfx("enemy_attack");
                }
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
        hi.framesLeft = 6;
        activeHits.append(hi);
    }

    // hitCode 5 = host killed blackgatmon → client gets enemy_attack (Competition only)
    // hitCode 6 = client killed blackgatmon → host gets enemy_attack (handled on server side)
    if (hitCode == 5 && m_gameMode != Cooperative) {
        enemyAttackActive = true;
        enemyAttackTimer.restart();
        m_audio->playSfx("enemy_attack");
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
            m_audio->playSfx("enemy_attack");
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
    m_rankingSaved = false;
    resetTargets();
}

