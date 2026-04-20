#ifndef GPIOBUTTON_H
#define GPIOBUTTON_H

#include <QObject>
#include <QTimer>

/*
    GPIO 버튼 입력 클래스 (SW2)

    /dev/gpiochip0 + ioctl 방식으로 GPIO 값을 직접 읽는다.
    커널 모듈 없이 동작하며, QTimer로 주기적으로 폴링하여
    falling edge (1→0) 감지 시 pressed() 시그널을 발생시킨다.

    사용법: GpioButton btn(4);  // BCM4 번 핀
            btn.start(50);       // 50ms 간격 폴링
*/
class GpioButton : public QObject
{
    Q_OBJECT

public:
    explicit GpioButton(int gpioNum = 4, QObject *parent = nullptr);
    ~GpioButton();

    /* 폴링 시작/중지 */
    void start(int intervalMs = 50);
    void stop();

signals:
    /* 버튼이 눌렸을 때 (falling edge: 1→0) */
    void pressed();

private slots:
    void poll();

private:
    int m_gpioNum;
    int m_chipFd;   /* /dev/gpiochip0 fd */
    int m_lineFd;   /* GPIO line handle fd */
    int m_lastValue;
    QTimer *m_timer;
};

#endif // GPIOBUTTON_H
