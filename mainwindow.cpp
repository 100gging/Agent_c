#include "mainwindow.h"

#include <QPainter>
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
    {"raremon", 1.0f, 1, 1},
    {"gajimon", 1.0f, 2, 3},
    {"picomon", 1.0f, 3, 5},
    {"oogamon", 2.0f, 2, 5},
};
static const EnemyDef ALLY_DEFS[] = {
    {"padakmon",  1.0f, 1, -1},
    {"dongulmon", 1.0f, 2, -3},
    {"agumon",    1.5f, 2, -2},
    {"tentamon",  1.5f, 3, -2},
    {"metamon",   1.0f, 1, -5},
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      timer(new QTimer(this)),
      gameState(Menu),
      aimStep(20),
      aimRadius(18),
      score(0),
      remainingTimeMs(60000),
      fireEffect(false),
      hitEffect(false),
      hitEffectFrames(0),
      lastHitWasEnemy(false),
      btnUp(nullptr),
      btnDown(nullptr),
      btnLeft(nullptr),
      btnRight(nullptr),
      btnFire(nullptr),
      btnStart(nullptr),
      btnNext(nullptr),
      btnRetry(nullptr),
      btnMainMenu(nullptr)
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

    backgroundPixmap = QPixmap(":/images/background.png");
    treePixmap = QPixmap(":/images/tree.png");
    bushPixmap = QPixmap(":/images/bush.png");
    leafPixmap = QPixmap(":/images/leaf.png");

    if (!treePixmap.isNull())
        treeMaskImage = treePixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    if (!bushPixmap.isNull())
        bushMaskImage = bushPixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    if (!leafPixmap.isNull())
        leafMaskImage = leafPixmap.toImage().convertToFormat(QImage::Format_ARGB32);

    // 초기 벽 위치 설정 (resizeEvent 전에 미리)
    treeRect = QRect(90, 80, 220, 360);
    bushRect = QRect(1024 - 380, 180, 250, 170);
    leafRect = QRect(1024 / 2 - 140, 130, 280, 160);

    setupUiButtons();
    resetTargets();
    connect(timer, &QTimer::timeout, this, &MainWindow::gameLoop);
    timer->start(30);
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUiButtons()
{
    btnUp = new QPushButton("UP", this);
    btnDown = new QPushButton("DOWN", this);
    btnLeft = new QPushButton("LEFT", this);
    btnRight = new QPushButton("RIGHT", this);
    btnFire = new QPushButton("FIRE", this);
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

    btnUp->setStyleSheet(commonStyle);
    btnDown->setStyleSheet(commonStyle);
    btnLeft->setStyleSheet(commonStyle);
    btnRight->setStyleSheet(commonStyle);
    btnRetry->setStyleSheet(commonStyle);
    btnMainMenu->setStyleSheet(commonStyle);
    btnNext->setStyleSheet(commonStyle);
    btnFire->setStyleSheet(fireStyle);
    btnStart->setStyleSheet(startStyle);

    for (QPushButton *btn : {btnUp, btnDown, btnLeft, btnRight, btnFire,
                              btnStart, btnNext, btnRetry, btnMainMenu}) {
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setAutoRepeat(false);
    }

    connect(btnUp,       &QPushButton::pressed, this, &MainWindow::moveUp);
    connect(btnDown,     &QPushButton::pressed, this, &MainWindow::moveDown);
    connect(btnLeft,     &QPushButton::pressed, this, &MainWindow::moveLeft);
    connect(btnRight,    &QPushButton::pressed, this, &MainWindow::moveRight);
    connect(btnFire,     &QPushButton::pressed, this, &MainWindow::fire);
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
    int maxY = qMax(minY + 1, height() - 270);
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
    return { randomPos(), sx, sy,
             (int)(BASE_R * d.size), d.points, true, d.name };
}

Target MainWindow::spawnAlly()
{
    int idx = QRandomGenerator::global()->bounded((int)(sizeof(ALLY_DEFS)/sizeof(ALLY_DEFS[0])));
    const EnemyDef &d = ALLY_DEFS[idx];
    int spd = d.speedTier * 3;
    int sx  = QRandomGenerator::global()->bounded(2) ? spd : -spd;
    int sy  = QRandomGenerator::global()->bounded(2) ? spd : -spd;
    return { randomPos(), sx, sy,
             (int)(BASE_R * d.size), d.points, false, d.name };
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
        if (pointHitsOpaquePixel(treeMaskImage, treeRect, p)) return true;
        if (pointHitsOpaquePixel(bushMaskImage, bushRect, p)) return true;
        if (pointHitsOpaquePixel(leafMaskImage, leafRect, p)) return true;
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
    if (!btnUp) return;

    int w = width();
    int h = height();

    int btnW = 110;
    int btnH = 70;

    // 방향 버튼
    int baseX = 40;
    int baseY = h - 220;

    btnUp->setGeometry(baseX + btnW, baseY, btnW, btnH);
    btnLeft->setGeometry(baseX, baseY + btnH + 10, btnW, btnH);
    btnRight->setGeometry(baseX + btnW * 2, baseY + btnH + 10, btnW, btnH);
    btnDown->setGeometry(baseX + btnW, baseY + (btnH + 10) * 2, btnW, btnH);

    // 기능 버튼
    btnFire->setGeometry(w - 220, h - 180, 180, 120);

    // Result 버튼
    btnRetry->setGeometry(w / 2 - 220, h / 2 + 80, 200, 70);
    btnMainMenu->setGeometry(w / 2 + 20, h / 2 + 80, 200, 70);

    // 메뉴 버튼
    btnStart->setGeometry(w / 2 - 140, h / 2 + 40, 280, 90);

    // 브리핑 다음 버튼
    btnNext->setGeometry(w / 2 - 140, h - 90, 280, 70);

    // 상태에 따라 버튼 표시/숨김
    bool isPlaying    = (gameState == Playing);
    bool isMenu       = (gameState == Menu);
    bool isCal        = (gameState == Calibrating);
    bool isBriefing   = (gameState == Briefing);
    bool isGameOver   = (gameState == GameOver);

    btnUp->setVisible(isPlaying || isCal);
    btnDown->setVisible(isPlaying || isCal);
    btnLeft->setVisible(isPlaying || isCal);
    btnRight->setVisible(isPlaying || isCal);
    btnFire->setVisible(isPlaying || isCal);
    btnStart->setVisible(isMenu);
    btnNext->setVisible(isBriefing);
    btnRetry->setVisible(isGameOver);
    btnMainMenu->setVisible(isGameOver);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    centerPos = QPoint(width() / 2, height() / 2);

    // 장애물 배치: 좌측 큰 나무 / 우측 중단 덤불 / 상단 중앙 큰 잎
    treeRect = QRect(90, qMax(70, height() - 520), 220, 360);
    bushRect = QRect(width() - 380, qMax(130, height() - 420), 250, 170);
    leafRect = QRect(width() / 2 - 140, 120, 280, 160);

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
                         "Press GAME START to begin");
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
            "You have 60 seconds. Good luck, Agent.";

        painter.drawText(rect().adjusted(80, -40, -80, -80), Qt::AlignLeft | Qt::TextWordWrap, briefing);
        return;
    }

    if (gameState == Calibrating) {
        // 캘리브레이션 화면
        painter.setFont(bigFont);
        painter.setPen(Qt::white);
        painter.drawText(rect().adjusted(0, -140, 0, 0), Qt::AlignCenter, "CALIBRATION");

        painter.setFont(subFont);
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(rect().adjusted(0, -60, 0, 0), Qt::AlignCenter,
                         "Align the aim (green) to the center target (red)\nthen press FIRE to confirm & start game");

        // 중앙 고정 타겟 (맞춰야 할 기준점)
        QPoint calTarget(width() / 2, height() / 2);
        painter.setPen(QPen(Qt::red, 3));
        painter.setBrush(QColor(200, 40, 40, 120));
        painter.drawEllipse(calTarget, 20, 20);
        painter.drawLine(calTarget.x() - 35, calTarget.y(), calTarget.x() + 35, calTarget.y());
        painter.drawLine(calTarget.x(), calTarget.y() - 35, calTarget.x(), calTarget.y() + 35);

        // 에임 표시
        painter.setPen(QPen(Qt::green, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(aimPos, aimRadius, aimRadius);
        painter.drawLine(aimPos.x() - 30, aimPos.y(), aimPos.x() + 30, aimPos.y());
        painter.drawLine(aimPos.x(), aimPos.y() - 30, aimPos.x(), aimPos.y() + 30);
        return;
    }

    // --- Playing / GameOver ---

    // 상단 정보
    painter.setPen(Qt::white);
    painter.setFont(infoFont);
    painter.drawText(20, 35, QString("Score: %1").arg(score));
    painter.drawText(20, 70, QString("Time: %1").arg(remainingTimeMs / 1000));

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
    if (!treePixmap.isNull())
        painter.drawPixmap(treeRect, treePixmap);
    else {
        painter.setBrush(QColor(80, 60, 40));
        painter.setPen(Qt::NoPen);
        painter.drawRect(treeRect);
    }

    if (!bushPixmap.isNull())
        painter.drawPixmap(bushRect, bushPixmap);
    else {
        painter.setBrush(QColor(30, 100, 40));
        painter.setPen(Qt::NoPen);
        painter.drawRect(bushRect);
    }

    if (!leafPixmap.isNull())
        painter.drawPixmap(leafRect, leafPixmap);
    else {
        painter.setBrush(QColor(40, 140, 60));
        painter.setPen(Qt::NoPen);
        painter.drawRect(leafRect);
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

    // 중앙 기준점
    painter.setPen(QPen(QColor(100, 100, 255), 2, Qt::DashLine));
    painter.drawLine(centerPos.x() - 15, centerPos.y(), centerPos.x() + 15, centerPos.y());
    painter.drawLine(centerPos.x(), centerPos.y() - 15, centerPos.x(), centerPos.y() + 15);

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
    if (gameState != Playing) {
        fireEffect = false;
        update();
        return;
    }

    remainingTimeMs -= 30;
    if (remainingTimeMs <= 0) {
        remainingTimeMs = 0;
        gameState = GameOver;
        updateButtonLayout();
    }

    // 각 타겟 이동
    int margin = 140;
    int minX = margin,     maxX = qMax(minX+1, width()  - margin);
    int minY = 110,        maxY = qMax(minY+1, height() - 270);

    for (Target &t : targets) {
        t.pos.rx() += t.speedX;
        t.pos.ry() += t.speedY;
        if (t.pos.x() < minX) { t.pos.setX(minX); t.speedX = qAbs(t.speedX); }
        if (t.pos.x() > maxX) { t.pos.setX(maxX); t.speedX = -qAbs(t.speedX); }
        if (t.pos.y() < minY) { t.pos.setY(minY); t.speedY = qAbs(t.speedY); }
        if (t.pos.y() > maxY) { t.pos.setY(maxY); t.speedY = -qAbs(t.speedY); }
    }

    // 명중 효과 프레임 감소
    if (hitEffectFrames > 0) {
        hitEffectFrames--;
        hitEffect = (hitEffectFrames > 0);
    }

    fireEffect = false;
    update();
}

void MainWindow::moveLeft()
{
    if (gameState != Playing && gameState != Calibrating) return;
    aimPos.rx() -= aimStep;
    clampAim();
    //update();
}

void MainWindow::moveRight()
{
    if (gameState != Playing && gameState != Calibrating) return;
    aimPos.rx() += aimStep;
    clampAim();
    //update();
}

void MainWindow::moveUp()
{
    if (gameState != Playing && gameState != Calibrating) return;
    aimPos.ry() -= aimStep;
    clampAim();
    //update();
}

void MainWindow::moveDown()
{
    if (gameState != Playing && gameState != Calibrating) return;
    aimPos.ry() += aimStep;
    clampAim();
    //update();
}

void MainWindow::startGame()
{
    // START 버튼 누르면 캘리브레이션으로
    enterCalibration();
}

void MainWindow::showBriefing()
{
    gameState = Briefing;
    updateButtonLayout();
    update();
}

void MainWindow::startPlaying()
{
    resetGame();
    gameState = Playing;
    updateButtonLayout();
    update();
}

void MainWindow::enterCalibration()
{
    gameState = Calibrating;
    aimPos = QPoint(width() / 2, height() / 2); // 에임을 화면 중앙에서 시작
    updateButtonLayout();
    update();
}

void MainWindow::fire()
{
    if (gameState == Calibrating) {
        centerPos = aimPos;
        showBriefing();
        return;
    }
    if (gameState != Playing) return;

    fireEffect = true;

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
    resetGame();
    gameState = Playing;
    updateButtonLayout();
    update();
}

void MainWindow::goToMainMenu()
{
    resetGame();
    gameState = Menu;
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
    remainingTimeMs = 60000;
    fireEffect = false;
    hitEffect = false;
    hitEffectFrames = 0;
    lastHitWasEnemy = false;
    aimPos = centerPos;
    resetTargets();
}

