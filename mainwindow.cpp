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
    // 2~3초 = 66~100 프레임 (30ms 간격)
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
      hitEffect(false),
      hitEffectFrames(0),
      lastHitWasEnemy(false),
      btnStart(nullptr),
      btnNext(nullptr),
      btnRetry(nullptr),
      btnMainMenu(nullptr),
      m_gpioBtn(new GpioButton(4, this)),
      m_sw2Btn(new GpioButton(17, this)),
      m_sw3Btn(new GpioButton(18, this)),
      settingsCursor(0),
      settingsBgmVol(50),
      settingsSfxVol(100),
      prevState(Menu)
{
    resize(1024, 600);
    setWindowTitle("Agent C");
    setStyleSheet("background-color: black;");

    centerPos = QPoint(width() / 2, height() / 2);
    aimPos = centerPos;

    // 모든 이미지 프리로드
    for (auto &d : ENEMY_DEFS)
        pixmaps[d.name] = QPixmap(QString(":/images/%1.png").arg(d.name));
    for (auto &d : ALLY_DEFS)
        pixmaps[d.name] = QPixmap(QString(":/images/%1.png").arg(d.name));

    for (auto it = pixmaps.constBegin(); it != pixmaps.constEnd(); ++it) {
        if (!it.value().isNull())
            spriteMasks[it.key()] = it.value().toImage().convertToFormat(QImage::Format_ARGB32);
    }

    backgroundPixmap = QPixmap(":/images/background_game.png");
    applePixmap = QPixmap(":/images/apple.png");

    howToPlay1Pixmap = QPixmap(":/images/how_to_play1.png");
    howToPlay2Pixmap = QPixmap(":/images/how_to_play2.png");
    howToPlay3Pixmap = QPixmap(":/images/how_to_play3.png");
    howToPlayPage = 0;
    howToPlayDurationMs = 3000;

    ready1Pixmap = QPixmap(":/images/ready_1.png");
    ready2Pixmap = QPixmap(":/images/ready_2.png");
    ready3Pixmap = QPixmap(":/images/ready_3.png");
    loadingPixmap = QPixmap(":/images/loading.png");
    countdownPage = 0;

    // apple.png 격자무늬(체커보드) 배경 제거 + 절반 크기
    if (!applePixmap.isNull()) {
        QImage appleImg = applePixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        for (int py = 0; py < appleImg.height(); ++py) {
            for (int px = 0; px < appleImg.width(); ++px) {
                QRgb pixel = appleImg.pixel(px, py);
                int r = qRed(pixel), g = qGreen(pixel), b = qBlue(pixel);
                int maxC = qMax(r, qMax(g, b));
                int minC = qMin(r, qMin(g, b));
                // 채도가 낮고 밝은 픽셀 = 체커보드 (회색/흰색)
                if ((maxC - minC) < 30 && minC > 170) {
                    appleImg.setPixel(px, py, qRgba(0, 0, 0, 0));
                }
            }
        }
        applePixmap = QPixmap::fromImage(appleImg).scaled(
            applePixmap.width() / 2, applePixmap.height() / 2,
            Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    treePixmap = QPixmap(":/images/tree.png");
    bushPixmap = QPixmap(":/images/bush.png");
    leafPixmap = QPixmap(":/images/leaf.png");

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

    // 초기 벽 위치 설정 (resizeEvent 전에 미리)
    treeRects[0] = QRect(60, 260, 180, 320);
    treeRects[1] = QRect(780, 240, 160, 300);
    bushRects[0] = QRect(744, 230, 200, 140);
    bushRects[1] = QRect(120, 200, 180, 130);
    leafRects[0] = QRect(272, -20, 240, 140);
    leafRects[1] = QRect(500, 30, 200, 130);
    leafRects[2] = QRect(700, 10, 200, 130);  // 반전 leaf

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

    /* 자이로 센서 화면 크기 설정 */
    m_sensor.setScreenSize(1024, 600);

    /* GPIO 버튼 연결 (BCM4) */
    connect(m_gpioBtn, &GpioButton::pressed, this, &MainWindow::onGpioPressed);
    m_gpioBtn->start(50);

    /* SW2 (BCM17): 아래 */
    connect(m_sw2Btn, &GpioButton::pressed, this, &MainWindow::onSw2Pressed);
    m_sw2Btn->start(50);

    /* SW3 (BCM18): 위/설정진입 */
    connect(m_sw3Btn, &GpioButton::pressed, this, &MainWindow::onSw3Pressed);
    m_sw3Btn->start(50);
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUiButtons()
{
    btnStart = new QPushButton("GAME START", this);
    btnNext = new QPushButton("START MISSION", this);
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

    QString fireStyle =
        "QPushButton {"
        "  background-color: rgba(180, 30, 30, 220);"
        "  color: white;"
        "  border: 2px solid white;"
        "  border-radius: 20px;"
        "  font-size: 26px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(255, 120, 120, 230);"
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
    btnNext->setStyleSheet(commonStyle);
    btnStart->setStyleSheet(startStyle);

    for (QPushButton *btn : {btnStart, btnNext, btnRetry, btnMainMenu}) {
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setAutoRepeat(false);
    }

    connect(btnStart,    &QPushButton::pressed, this, &MainWindow::startGame);
    connect(btnNext,     &QPushButton::pressed, this, &MainWindow::startPlaying);
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
    // 지상 유닛은 하반부에 스폰
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
    // 버튼이 아직 생성되지 않은 경우 무시
    if (!btnStart) return;

    int w = width();
    int h = height();

    // 기능 버튼

    // Result 버튼
    btnRetry->setGeometry(w / 2 - 220, h / 2 + 80, 200, 70);
    btnMainMenu->setGeometry(w / 2 + 20, h / 2 + 80, 200, 70);

    // 메뉴 버튼
    btnStart->setGeometry(w / 2 - 140, h / 2 + 40, 280, 90);

    // 브리핑 다음 버튼
    btnNext->setGeometry(w / 2 - 140, h - 90, 280, 70);

    // 상태에 따라 버튼 표시/숨김
    bool isMenu       = (gameState == Menu);
    bool isBriefing   = (gameState == Briefing);
    bool isGameOver   = (gameState == GameOver);

    btnStart->setVisible(isMenu);
    btnNext->setVisible(isBriefing);
    btnRetry->setVisible(isGameOver);
    btnMainMenu->setVisible(isGameOver);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    centerPos = QPoint(width() / 2, height() / 2);

    // 장애물 배치: tree x2, bush x2, leaf x3
    treeRects[0] = QRect(90, height() / 2 - 60, 180, 320);
    treeRects[1] = QRect(width() - 220, height() - 310, 160, 300);
    bushRects[0] = QRect(225, height() / 2 + 35, 250, 175);
    bushRects[1] = QRect(width() - 500, height() / 2 - 40, 200, 140);
    leafRects[0] = QRect(width() / 2 - 240, 85, 120, 140);
    leafRects[1] = QRect(width() - 250, 60, 170, 180);
    leafRects[2] = QRect(width() / 2 - 90, 30, 150, 160);  // 반전 leaf
    updateButtonLayout();
    clampAim();
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

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
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(rect().adjusted(0, 40, 0, 0), Qt::AlignCenter,
                         "Press BUTTON to begin");
        return;
    }

    if (gameState == Settings) {
        // 환경설정 화면 - 어두운 배경
        painter.fillRect(rect(), QColor(15, 15, 15));

        painter.setFont(bigFont);
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(rect().adjusted(0, -200, 0, 0), Qt::AlignCenter, "SETTINGS");

        QFont itemFont("Arial", 24, QFont::Bold);
        painter.setFont(itemFont);

        int startY = height() / 2 - 60;
        int lineH = 70;

        QString items[3] = {
            QString("BGM Volume: %1").arg(settingsBgmVol),
            QString("SFX Volume: %1").arg(settingsSfxVol),
            QString("EXIT")
        };

        for (int i = 0; i < 3; i++) {
            QRect itemRect(width() / 2 - 200, startY + i * lineH, 400, 50);

            if (i == settingsCursor) {
                // 선택된 항목: 하이라이트
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

        // 하단 안내
        painter.setFont(QFont("Arial", 14));
        painter.setPen(QColor(120, 120, 120));
        painter.drawText(rect().adjusted(0, 270, 0, 0), Qt::AlignCenter,
                         "SW2: DOWN  |  SW3: UP  |  FIRE: SELECT");
        return;
    }

    if (gameState == HowToPlay) {
        // 플레이 방법 안내 이미지 전체화면 표시
        const QPixmap &pm = (howToPlayPage == 0) ? howToPlay1Pixmap
                          : (howToPlayPage == 1) ? howToPlay2Pixmap
                          : howToPlay3Pixmap;
        if (!pm.isNull())
            painter.drawPixmap(rect(), pm);
        else
            painter.fillRect(rect(), QColor(10, 10, 10));
        return;
    }

    if (gameState == Briefing) {
        // 스토리 / 브리핑 화면
        painter.setFont(bigFont);
        painter.setPen(QColor(255, 60, 60));
        painter.drawText(rect().adjusted(0, -200, 0, 0), Qt::AlignCenter, "MISSION BRIEFING");

        QFont briefFont("Arial", 17);
        painter.setFont(briefFont);
        painter.setPen(Qt::white);

        QString briefing =
            "Agent C, listen carefully.\n\n"
            "Hostile targets have been spotted in the area.\n"
            "Your mission: eliminate all RED targets on sight.\n\n"
            "[SHOOT]  Red enemy targets\n"
            "[DO NOT SHOOT]  Civilians (non-red)\n\n"
            "Use the directional buttons to aim.\n"
            "Press FIRE to shoot.\n\n"
            "You have 30 seconds. Good luck, Agent.";

        painter.drawText(rect().adjusted(80, -40, -80, -80), Qt::AlignLeft | Qt::TextWordWrap, briefing);
        return;
    }

    if (gameState == Countdown) {
        // ready_3 → ready_2 → ready_1 전체화면 표시
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
        // 캘리브레이션 화면
        painter.setFont(bigFont);
        painter.setPen(Qt::white);
        painter.drawText(rect().adjusted(0, -140, 0, 0), Qt::AlignCenter, "CALIBRATION");

        if (calPhase == 0) {
            // Phase 0: 영점 조절 — 사과만 보이고 조준선 없음
            painter.setFont(subFont);
            painter.setPen(QColor(180, 180, 180));
            painter.drawText(rect().adjusted(0, -60, 0, 0), Qt::AlignCenter,
                             "Aim at the apple and press FIRE to zero in");

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
            // 조준선 없음 (영점 전)
        } else {
            // Phase 1: 3박스 타겟 연습
            painter.setFont(subFont);
            painter.setPen(QColor(180, 180, 180));
            painter.drawText(rect().adjusted(0, -60, 0, 0), Qt::AlignCenter,
                             "Shoot all 3 targets to start the mission!");

            int boxW = 70, boxH = 50;
            int by = height() - 180;
            QRect calBoxes[3] = {
                QRect(width() / 4 - boxW / 2, by, boxW, boxH),
                QRect(width() / 2 - boxW / 2, by, boxW, boxH),
                QRect(3 * width() / 4 - boxW / 2, by, boxW, boxH),
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

            // 에임 표시 (영점 완료 후 보임)
            painter.setPen(QPen(Qt::green, 3));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(aimPos, aimRadius, aimRadius);
            painter.drawLine(aimPos.x() - 30, aimPos.y(), aimPos.x() + 30, aimPos.y());
            painter.drawLine(aimPos.x(), aimPos.y() - 30, aimPos.x(), aimPos.y() + 30);
        }
        return;
    }

    // --- Playing / GameOver ---

    // 상단 정보
    painter.setPen(Qt::white);
    painter.setFont(infoFont);
    painter.drawText(20, 35, QString("Score: %1").arg(score));
    int remainingTimeMs = gameDurationMs - (int)gameElapsed.elapsed();
    if (remainingTimeMs < 0) remainingTimeMs = 0;
    QString timeStr = QString("Time: %1").arg(remainingTimeMs / 1000);
    painter.drawText(width() - 180, 35, timeStr);

    // 1. 타겟을 먼저 그림 (벽 뒤에 숨겨지도록)
    if (gameState == Playing || gameState == GameOver) {
        for (const Target &t : targets) {
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

    // 2. 장애물을 나중에 그림 (타겟 위에 덮임)
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
    // 반전 leaf (leafRects[2])
    if (!leafFlippedPixmap.isNull())
        painter.drawPixmap(leafRects[2], leafFlippedPixmap);
    else {
        painter.setBrush(QColor(40, 140, 60));
        painter.setPen(Qt::NoPen);
        painter.drawRect(leafRects[2]);
    }

    // 에임
    painter.setPen(QPen(Qt::green, 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(aimPos, aimRadius, aimRadius);
    painter.drawLine(aimPos.x() - 25, aimPos.y(), aimPos.x() + 25, aimPos.y());
    painter.drawLine(aimPos.x(), aimPos.y() - 25, aimPos.x(), aimPos.y() + 25);

    // 발사 효과
    if (fireEffect) {
        painter.setPen(QPen(Qt::yellow, 4));
        painter.drawEllipse(aimPos, aimRadius + 10, aimRadius + 10);
        painter.drawText(aimPos.x() + 25, aimPos.y() - 25, "FIRE!");
    }

    // 명중 효과
    if (hitEffect) {
        painter.setPen(QPen(Qt::white, 5));
        QColor hc = lastHitWasEnemy ? QColor(255, 200, 0, 160) : QColor(80, 180, 255, 160);
        painter.setBrush(hc);
        painter.drawEllipse(aimPos, aimRadius + 20, aimRadius + 20);
        painter.setPen(lastHitWasEnemy ? Qt::yellow : Qt::cyan);
        painter.setFont(QFont("Arial", 22, QFont::Bold));
        painter.drawText(aimPos.x() + 25, aimPos.y() - 30,
                         lastHitWasEnemy ? "+HIT!" : "-ALLY!");
    }

    // 게임 종료 화면
    if (gameState == GameOver) {
        painter.fillRect(rect(), QColor(0, 0, 0, 180));
        painter.setFont(bigFont);
        painter.setPen(Qt::white);
        painter.drawText(rect().adjusted(0, -100, 0, 0), Qt::AlignCenter,
                         QString("GAME OVER"));
        painter.setFont(QFont("Arial", 26, QFont::Bold));
        painter.setPen(QColor(255, 220, 50));
        painter.drawText(rect().adjusted(0, -20, 0, 0), Qt::AlignCenter,
                         QString("Final Score: %1").arg(score));
    }
}

void MainWindow::gameLoop()
{
    if (gameState == HowToPlay) {
        int elapsed = (int)howToPlayElapsed.elapsed();
        if (elapsed >= howToPlayDurationMs) {
            if (howToPlayPage < 2) {
                howToPlayPage++;
                howToPlayDurationMs = 3000;
                howToPlayElapsed.restart();
            } else {
                showBriefing();
            }
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
                // 카운트다운 완료 → 게임 시작
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
        if (gameState == Calibrating && calPhase == 1) {
            /* 캘리브레이션 Phase 1: 센서로 에임 이동 */
            m_sensor.update();
            aimPos.setX(m_sensor.aimX());
            aimPos.setY(m_sensor.aimY());
            clampAim();
        }
        fireEffect = false;
        update();
        return;
    }

    int remainingMs = gameDurationMs - (int)gameElapsed.elapsed();
    if (remainingMs <= 0) {
        remainingMs = 0;
        gameState = GameOver;
        m_audio->stopBgm();
        updateButtonLayout();
    }

    /* 자이로 센서로 에임 이동 */
    m_sensor.update();
    aimPos.setX(m_sensor.aimX());
    aimPos.setY(m_sensor.aimY());
    clampAim();

    // 각 타겟 이동
    int margin = 120;
    int minX = margin,     maxX = qMax(minX+1, width()  - margin);
    int minY = 110,        maxY = qMax(minY+1, height() - 120);

    for (Target &t : targets) {
        // 지상 유닛: 방향 전환 타이머
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

        // 지상 유닛: Y 범위를 하반부로 제한
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

    // 명중 효과 프레임 감소
    if (hitEffectFrames > 0) {
        hitEffectFrames--;
        hitEffect = (hitEffectFrames > 0);
    }

    fireEffect = false;
    update();
}

/* ================================================================
   GPIO 버튼 (BCM4) 눌림 처리
   상태에 따라 다른 동작:
   - Menu → 게임 시작 (startGame)
   - Calibrating → fire (영점/타겟)
   - Briefing → startPlaying
   - Playing → fire (총 발사)
   - GameOver → retryGame
   ================================================================ */
void MainWindow::onGpioPressed()
{
    switch (gameState) {
    case Settings:
        // 설정 창에서 BCM4 = 선택
        if (settingsCursor == 0) {
            // BGM 볼륨 변경: 0→25→50→75→100→0
            settingsBgmVol = (settingsBgmVol >= 100) ? 0 : settingsBgmVol + 25;
            m_audio->setBgmVolume(settingsBgmVol);
        } else if (settingsCursor == 1) {
            // SFX 볼륨 변경: 0→25→50→75→100→0
            settingsSfxVol = (settingsSfxVol >= 100) ? 0 : settingsSfxVol + 25;
            m_audio->setSfxVolume(settingsSfxVol);
        } else {
            // 나가기
            gameState = prevState;
            updateButtonLayout();
        }
        update();
        break;
    case Menu:
        startGame();
        break;
    case GyroCalibrating:
        /* 자이로 보정 중에는 무시 */
        break;
    case Calibrating:
        fire();
        break;
    case HowToPlay:
    case Countdown:
        /* 진행 중 무시 */
        break;
    case Briefing:
        startPlaying();
        break;
    case Playing:
        fire();
        break;
    case GameOver:
        retryGame();
        break;
    }
}

/* SW2 (BCM17): 설정 창에서 아래로 */
void MainWindow::onSw2Pressed()
{
    if (gameState == Settings) {
        settingsCursor++;
        if (settingsCursor > 2) settingsCursor = 0;
        update();
    }
}

/* SW3 (BCM18): 설정 창 진입 / 설정 창에서 위로 */
void MainWindow::onSw3Pressed()
{
    if (gameState == Settings) {
        settingsCursor--;
        if (settingsCursor < 0) settingsCursor = 2;
        update();
    } else if (gameState == Menu) {
        // 메뉴에서 SW3 누르면 설정 진입
        prevState = gameState;
        settingsCursor = 0;
        gameState = Settings;
        updateButtonLayout();
        update();
    }
}

void MainWindow::startGame()
{
    // START 버튼 누르면 캘리브레이션으로
    enterCalibration();
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

void MainWindow::showBriefing()
{
    gameState = Briefing;
    updateButtonLayout();
    update();
}

void MainWindow::startPlaying()
{
    showCountdown();
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
    /* 자이로 센서 초기화 + 보정 */
    gameState = GyroCalibrating;
    updateButtonLayout();
    update();
    QApplication::processEvents();

    if (m_sensor.init()) {
        m_sensor.calibrateGyroOffset(500);
        m_sensor.calibrateCenter(200);
    }

    gameState = Calibrating;
    calPhase = 0;
    aimPos = QPoint(width() / 2, height() / 2);
    calBoxHit[0] = calBoxHit[1] = calBoxHit[2] = false;
    updateButtonLayout();
    update();
}

void MainWindow::fire()
{
    if (gameState == Calibrating) {
        fireEffect = true;
        m_audio->playSfx("fire");

        if (calPhase == 0) {
            // 영점 조절: 현재 aimPos를 centerPos로 저장, 조준선을 화면 중앙으로
            centerPos = aimPos;
            aimPos = QPoint(width() / 2, height() / 2);
            calPhase = 1;
        } else {
            // 3박스 타겟 히트 체크
            int boxW = 70, boxH = 50;
            int by = height() - 180;
            QRect calBoxes[3] = {
                QRect(width() / 4 - boxW / 2, by, boxW, boxH),
                QRect(width() / 2 - boxW / 2, by, boxW, boxH),
                QRect(3 * width() / 4 - boxW / 2, by, boxW, boxH),
            };
            for (int i = 0; i < 3; i++) {
                if (!calBoxHit[i] && calBoxes[i].contains(aimPos)) {
                    calBoxHit[i] = true;
                    break;
                }
            }
            if (calBoxHit[0] && calBoxHit[1] && calBoxHit[2]) {
                showHowToPlay();
            }
        }
        // update();
        return;
    }
    if (gameState != Playing) return;

    fireEffect = true;
    m_audio->playSfx("fire");

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

        if (hitTarget && !isBlockedByWall(aimPos, t.pos)) {
            score += t.points;
            lastHitWasEnemy = t.isEnemy;
            hitEffect = true;
            hitEffectFrames = 12;
            if (t.isEnemy)
                m_audio->playSfx("enemy_dead");
            else
                m_audio->playSfx("ally_dead");
            // 해당 타겟을 같은 타입으로 리스폰
            if (t.isEnemy)
                targets[i] = spawnEnemy();
            else
                targets[i] = spawnAlly();
            break; // 한 벌 사격에 하나만 명중
        }
    }

    update();
}

void MainWindow::retryGame()
{
    showCountdown();
}

void MainWindow::goToMainMenu()
{
    resetGame();
    gameState = Menu;
    centerPos = QPoint(width() / 2, height() / 2);
    aimPos = centerPos;
    m_audio->playBgm("menu");
    updateButtonLayout();
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
    hitEffect = false;
    hitEffectFrames = 0;
    lastHitWasEnemy = false;
    aimPos = centerPos;
    resetTargets();
}

