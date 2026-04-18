#include "gpiobutton.h"
#include <QDebug>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#endif

GpioButton::GpioButton(int gpioNum, QObject *parent)
    : QObject(parent)
    , m_gpioNum(gpioNum)
    , m_chipFd(-1)
    , m_lineFd(-1)
    , m_lastValue(1)   /* 풀업: 기본 = 1 (안 누름) */
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &GpioButton::poll);
}

GpioButton::~GpioButton()
{
    stop();
}

void GpioButton::start(int intervalMs)
{
#ifdef __linux__
    /* /dev/gpiochip0 열기 */
    m_chipFd = open("/dev/gpiochip0", O_RDONLY);
    if (m_chipFd < 0) {
        qWarning() << "GpioButton: cannot open /dev/gpiochip0"
                   << "— 버튼 없이 계속 실행합니다";
    } else {
        /* GPIO line handle 요청 (INPUT) */
        struct gpiohandle_request req;
        memset(&req, 0, sizeof(req));
        req.lineoffsets[0] = m_gpioNum;
        req.flags = GPIOHANDLE_REQUEST_INPUT;
        req.lines = 1;
        strcpy(req.consumer_label, "qt_button");

        if (ioctl(m_chipFd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
            qWarning() << "GpioButton: GPIO_GET_LINEHANDLE_IOCTL failed for BCM"
                       << m_gpioNum << "— 버튼 없이 계속 실행합니다";
        } else {
            m_lineFd = req.fd;
            qDebug() << "GpioButton: /dev/gpiochip0 BCM" << m_gpioNum << "ready";
        }
    }
#endif
    m_timer->start(intervalMs);
}

void GpioButton::stop()
{
    m_timer->stop();
#ifdef __linux__
    if (m_lineFd >= 0) { close(m_lineFd); m_lineFd = -1; }
    if (m_chipFd >= 0) { close(m_chipFd); m_chipFd = -1; }
#endif
}

/*
    주기적으로 GPIO 값을 ioctl로 읽어서
    1 → 0 (falling edge) 감지 시 pressed() 시그널 발생
*/
void GpioButton::poll()
{
#ifdef __linux__
    if (m_lineFd < 0) return;

    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));

    if (ioctl(m_lineFd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0)
        return;

    int val = data.values[0];

    /* falling edge 감지: 이전 1 → 현재 0 = 버튼 눌림 */
    if (m_lastValue == 1 && val == 0) {
        emit pressed();
    }

    m_lastValue = val;
#endif
}
