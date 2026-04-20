#include "mpu6050sensor.h"

#ifdef __linux__
#  include <linux/i2c-dev.h>
#  include <sys/ioctl.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

#include <cstdio>
#include <cstring>

/* MPU6050 레지스터 주소 */
#define MPU6050_ADDR     0x68
#define PWR_MGMT_1       0x6B
#define ACCEL_XOUT_H     0x3B
#define REG_SMPLRT_DIV   0x19
#define REG_CONFIG       0x1A
#define REG_GYRO_CONFIG  0x1B

/* ─── Linux I2C 헬퍼 함수 ─── */

#ifdef __linux__
static int i2c_write_byte(int fd, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return (write(fd, buf, 2) == 2) ? 0 : -1;
}

static int i2c_read_bytes(int fd, uint8_t reg, uint8_t *buf, int len)
{
    if (write(fd, &reg, 1) != 1) return -1;
    if (read(fd, buf, len) != len) return -1;
    return 0;
}
#endif

/* ─── 생성자 / 소멸자 ─── */

MPU6050Sensor::MPU6050Sensor()
    : m_fd(-1)
    , m_screenW(1024), m_screenH(600)
    , m_gyroOffsetX(0), m_gyroOffsetY(0), m_gyroOffsetZ(0)
    , m_basePitch(0), m_baseRoll(0)
    , m_filteredPitch(0), m_filteredRoll(0)
    , m_sensitivityX(15.0), m_sensitivityY(15.0)
    , m_deadzoneDeg(2.0)
    , m_aimX(512), m_aimY(300)
    , m_firstUpdate(true)
    , m_calibrated(false)
    , m_gyroXDps(0), m_gyroYDps(0)
    , m_accelPitch(0), m_accelRoll(0)
    , m_deltaPitch(0), m_deltaRoll(0)
{
    memset(&m_prevTime, 0, sizeof(m_prevTime));
    memset(&m_lastRaw, 0, sizeof(m_lastRaw));
}

MPU6050Sensor::~MPU6050Sensor()
{
#ifdef __linux__
    if (m_fd >= 0) close(m_fd);
#endif
}

/* ─── 초기화 ─── */

bool MPU6050Sensor::init(const char *dev)
{
#ifdef __linux__
    m_fd = open(dev, O_RDWR);
    if (m_fd < 0) {
        perror("MPU6050 open");
        return false;
    }

    if (ioctl(m_fd, I2C_SLAVE, MPU6050_ADDR) < 0) {
        perror("MPU6050 ioctl");
        close(m_fd);
        m_fd = -1;
        return false;
    }

    /* 센서 깨우기: CLKSEL=001 → 자이로 X축 PLL 클럭 (데이터시트 권장) */
    if (i2c_write_byte(m_fd, PWR_MGMT_1, 0x01) < 0) {
        close(m_fd);
        m_fd = -1;
        return false;
    }
    usleep(100000);  /* 100ms 안정화 대기 */

    /* 센서 동작 설정 */
    i2c_write_byte(m_fd, REG_SMPLRT_DIV, 0x07);   /* 샘플링 125Hz */
    i2c_write_byte(m_fd, REG_CONFIG,     0x00);    /* DLPF 비활성화 */
    i2c_write_byte(m_fd, REG_GYRO_CONFIG,0x00);    /* ±250°/s */

    return true;
#else
    (void)dev;
    return false;  /* Linux가 아니면 센서 없음 */
#endif
}

/* ─── raw 데이터 읽기 ─── */

bool MPU6050Sensor::readRaw(RawData *raw)
{
#ifdef __linux__
    if (m_fd < 0) return false;

    uint8_t buf[14];
    /*
        ACCEL_XOUT_H(0x3B)부터 14바이트 연속 읽기
        [0-1] accel_x, [2-3] accel_y, [4-5] accel_z,
        [6-7] temp, [8-9] gyro_x, [10-11] gyro_y, [12-13] gyro_z
    */
    if (i2c_read_bytes(m_fd, ACCEL_XOUT_H, buf, 14) < 0)
        return false;

    raw->accel_x = (int16_t)((buf[0]  << 8) | buf[1]);
    raw->accel_y = (int16_t)((buf[2]  << 8) | buf[3]);
    raw->accel_z = (int16_t)((buf[4]  << 8) | buf[5]);
    raw->temp    = (int16_t)((buf[6]  << 8) | buf[7]);
    raw->gyro_x  = (int16_t)((buf[8]  << 8) | buf[9]);
    raw->gyro_y  = (int16_t)((buf[10] << 8) | buf[11]);
    raw->gyro_z  = (int16_t)((buf[12] << 8) | buf[13]);
    return true;
#else
    (void)raw;
    return false;
#endif
}

/* ─── 가속도 → pitch/roll 각도 계산 ─── */

void MPU6050Sensor::calcAccelAngle(const RawData &raw, double &pitch, double &roll)
{
    /* raw 값을 g 단위로 변환 */
    double ax = raw.accel_x / ACCEL_SCALE;
    double ay = raw.accel_y / ACCEL_SCALE;
    double az = raw.accel_z / ACCEL_SCALE;

    /*
        pitch (상하): 센서 Y축 회전 → accel_x 가 중력에 의해 변함
        yaw  (좌우): 센서 Z축 회전 → 가속도만으로는 측정 불가 (0 고정)
                     yaw는 update()에서 gyro_z 적분으로만 계산함
    */
    pitch = atan2(-ax, sqrt(ay * ay + az * az)) * RAD2DEG;
    roll  = 0.0;  /* yaw는 가속도로 측정 불가 → 자이로만 사용 */
}

/* ─── 자이로 오프셋 보정 ─── */

void MPU6050Sensor::calibrateGyroOffset(int samples)
{
    /*
        총을 가만히 둔 상태에서 여러 번 읽어 평균을 구한다.
        이 평균이 "아무것도 안 했는데 나오는 노이즈"이므로
        이후 측정값에서 빼준다.
    */
    long sumX = 0, sumY = 0, sumZ = 0;
    RawData raw;

    for (int i = 0; i < samples; i++) {
        if (readRaw(&raw)) {
            sumX += raw.gyro_x;
            sumY += raw.gyro_y;
            sumZ += raw.gyro_z;
        }
#ifdef __linux__
        usleep(5000);  /* 5ms 간격 */
#endif
    }

    m_gyroOffsetX = (double)sumX / samples;
    m_gyroOffsetY = (double)sumY / samples;
    m_gyroOffsetZ = (double)sumZ / samples;
}

/* ─── 중앙 기준값 보정 ─── */

void MPU6050Sensor::calibrateCenter(int samples)
{
    /*
        사용자가 화면 중앙을 향해 정조준한 상태에서
        200샘플(약 2초)의 pitch/roll 평균을 구한다.
        이 값이 "에임 중앙"의 기준 자세가 된다.
    */
    double sumPitch = 0.0, sumRoll = 0.0;
    RawData raw;

    for (int i = 0; i < samples; i++) {
        if (readRaw(&raw)) {
            double p, r;
            calcAccelAngle(raw, p, r);
            sumPitch += p;
            sumRoll  += r;
        }
#ifdef __linux__
        usleep(10000);  /* 10ms 간격 → 총 200 × 10ms = 2초 */
#endif
    }

    m_basePitch = sumPitch / samples;
    m_baseRoll  = sumRoll  / samples;

    /* 필터 초기값도 기준값으로 맞춰야 처음에 값이 튀지 않음 */
    m_filteredPitch = m_basePitch;
    m_filteredRoll  = m_baseRoll;

    m_firstUpdate = true;
    m_calibrated  = true;
}

/* ─── 영점 즉시 재설정 ─── */

void MPU6050Sensor::rezero()
{
    m_basePitch = m_filteredPitch;
    m_baseRoll  = m_filteredRoll;
}

/* ─── 매 프레임 업데이트 ─── */

void MPU6050Sensor::update()
{
    RawData raw;
    if (!readRaw(&raw)) return;
    m_lastRaw = raw;  /* 디버그용 raw값 보관 */

    /* dt 계산 (이전 프레임과 현재 프레임 사이 시간, 단위: 초) */
    struct timespec now;
#ifdef __linux__
    clock_gettime(CLOCK_MONOTONIC, &now);
#else
    memset(&now, 0, sizeof(now));
#endif

    double dt = 0.01;  /* 기본값 10ms */
    if (!m_firstUpdate) {
        dt = (now.tv_sec  - m_prevTime.tv_sec)
           + (now.tv_nsec - m_prevTime.tv_nsec) / 1000000000.0;
        if (dt <= 0.0 || dt > 1.0) dt = 0.01;  /* 이상값 방지 */
    }
    m_prevTime    = now;
    m_firstUpdate = false;

    /* 1) 가속도 기반 현재 기울기 계산 */
    calcAccelAngle(raw, m_accelPitch, m_accelRoll);

    /* 2) 자이로 deg/s 변환 (오프셋 빼기)
       pitch(상하) ← gyro_y  (센서 Y축 회전)
       yaw (좌우) ← gyro_z  (센서 Z축 회전) */
    m_gyroXDps = (raw.gyro_y - m_gyroOffsetY) / GYRO_SCALE;  /* pitch용 */
    m_gyroYDps = (raw.gyro_z - m_gyroOffsetZ) / GYRO_SCALE;  /* yaw용  */
    double gPitch = m_gyroXDps;
    double gYaw   = m_gyroYDps;

    /*
        3) Complementary Filter
        pitch: 가속도 보정 가능 → 98% 자이로 + 2% 가속도
        yaw  : 가속도로 측정 불가 → 100% 자이로 적분만 사용
    */
    m_filteredPitch = 0.98 * (m_filteredPitch + gPitch * dt) + 0.02 * m_accelPitch;
    m_filteredRoll  = m_filteredRoll + gYaw * dt;  /* 자이로만 적분 */

    /* 4) 기준값과의 차이 계산 — 디버그용으로 멤버에 저장 */
    m_deltaPitch = m_filteredPitch - m_basePitch;
    m_deltaRoll  = m_filteredRoll  - m_baseRoll;
    double deltaPitch = m_deltaPitch;
    double deltaRoll  = m_deltaRoll;

    /* 5) 데드존 제거 — 원시 값 그대로 사용 */

    /* 6) 화면 좌표 변환
       roll  → 좌우 이동
       pitch → 상하 이동 (위로 들면 y 감소이므로 음수 부호) */
    int cx = m_screenW / 2;
    int cy = m_screenH / 2;

    m_aimX = cx - (int)(deltaRoll  * m_sensitivityX);
    m_aimY = cy - (int)(deltaPitch * m_sensitivityY);

    /* 7) 화면 범위 내로 클램핑 */
    if (m_aimX < 0)          m_aimX = 0;
    if (m_aimX >= m_screenW) m_aimX = m_screenW - 1;
    if (m_aimY < 0)          m_aimY = 0;
    if (m_aimY >= m_screenH) m_aimY = m_screenH - 1;
}
