#ifndef MPU6050SENSOR_H
#define MPU6050SENSOR_H

#include <cstdint>
#include <cmath>
#include <ctime>

/*
    MPU6050 센서 래퍼 클래스
    gyroaim.c 로직을 Qt C++ 클래스로 구현한 것

    기능:
    - I2C 통신으로 MPU6050에서 가속도 3축 + 자이로 3축 읽기
    - 자이로 오프셋 보정 (500샘플)
    - 중앙 기준값 보정 (200샘플)
    - Complementary Filter 적용 (자이로 98% + 가속도 2%)
    - 데드존 처리 (2도 이내 무시)
    - 화면 에임 좌표 계산 및 클램핑
*/
class MPU6050Sensor
{
public:
    MPU6050Sensor();
    ~MPU6050Sensor();

    /* I2C 디바이스 열기 및 센서 초기화 (성공 시 true) */
    bool init(const char *dev = "/dev/i2c-1");

    /* 자이로 오프셋 보정 — 정지 상태에서 호출해야 정확함 */
    void calibrateGyroOffset(int samples = 500);

    /* 중앙 기준값 보정 — 정조준 상태에서 호출해야 함 */
    void calibrateCenter(int samples = 200);

    /* 센서 읽기 + 필터 적용 + 에임 좌표 계산 (매 프레임 호출) */
    void update();

    /* 현재 에임 좌표 반환 */
    int aimX() const { return m_aimX; }
    int aimY() const { return m_aimY; }

    /* 보정 완료 여부 */
    bool isCalibrated() const { return m_calibrated; }

    /* 화면 크기 설정 */
    void setScreenSize(int w, int h) { m_screenW = w; m_screenH = h; }

    /* 민감도 설정 (각도 1도 = 픽셀 몇 칸) */
    void setSensitivity(double sx, double sy) { m_sensitivityX = sx; m_sensitivityY = sy; }

    /* ── 디버그용 getter ── */
    /* 최근 raw 값 */
    int16_t rawAccelX() const { return m_lastRaw.accel_x; }
    int16_t rawAccelY() const { return m_lastRaw.accel_y; }
    int16_t rawAccelZ() const { return m_lastRaw.accel_z; }
    int16_t rawGyroX()  const { return m_lastRaw.gyro_x;  }
    int16_t rawGyroY()  const { return m_lastRaw.gyro_y;  }
    int16_t rawGyroZ()  const { return m_lastRaw.gyro_z;  }

    /* 오프셋 보정 후 자이로 deg/s */
    double gyroXDps() const { return m_gyroXDps; }
    double gyroYDps() const { return m_gyroYDps; }

    /* 가속도 기반 각도 */
    double accelPitch() const { return m_accelPitch; }
    double accelRoll()  const { return m_accelRoll;  }

    /* complementary filter 결과 각도 */
    double filteredPitch() const { return m_filteredPitch; }
    double filteredRoll()  const { return m_filteredRoll;  }

    /* 기준값 */
    double basePitch() const { return m_basePitch; }
    double baseRoll()  const { return m_baseRoll;  }

    /* 기준값과의 차이 (데드존 적용 전) */
    double deltaPitch() const { return m_deltaPitch; }
    double deltaRoll()  const { return m_deltaRoll;  }

    /* 자이로 오프셋 */
    double gyroOffsetX() const { return m_gyroOffsetX; }
    double gyroOffsetY() const { return m_gyroOffsetY; }

private:
    /* MPU6050 raw 데이터 구조체 */
    struct RawData {
        int16_t accel_x, accel_y, accel_z;
        int16_t temp;
        int16_t gyro_x, gyro_y, gyro_z;
    };

    bool readRaw(RawData *raw);
    void calcAccelAngle(const RawData &raw, double &pitch, double &roll);

    int m_fd;               /* I2C 파일 디스크립터 */

    int m_screenW;          /* 화면 가로 크기 */
    int m_screenH;          /* 화면 세로 크기 */

    /* 자이로 오프셋 — 정지 상태에서의 노이즈 평균 */
    double m_gyroOffsetX;
    double m_gyroOffsetY;
    double m_gyroOffsetZ;

    /* 중앙 기준 각도 — 정조준 자세에서의 pitch/roll */
    double m_basePitch;
    double m_baseRoll;

    /* complementary filter 누적 결과 */
    double m_filteredPitch;
    double m_filteredRoll;

    /* 민감도 — 각도 1도 차이를 픽셀 몇 칸으로 변환할지 */
    double m_sensitivityX;
    double m_sensitivityY;

    /* 데드존 — 이 각도 이내의 움직임은 손떨림으로 보고 무시 */
    double m_deadzoneDeg;

    /* 에임 좌표 */
    int m_aimX;
    int m_aimY;

    /* 디버그용 중간값 보관 */
    RawData m_lastRaw;
    double  m_gyroXDps;
    double  m_gyroYDps;
    double  m_accelPitch;
    double  m_accelRoll;
    double  m_deltaPitch;
    double  m_deltaRoll;

    /* dt 계산용 시간 */
    struct timespec m_prevTime;
    bool m_firstUpdate;

    bool m_calibrated;

    /* 센서 스케일 상수 */
    static constexpr double ACCEL_SCALE = 16384.0;  /* ±2g → 16384 LSB/g */
    static constexpr double GYRO_SCALE  = 131.0;    /* ±250°/s → 131 LSB/(°/s) */
    static constexpr double RAD2DEG     = 57.2957795;
};

#endif // MPU6050SENSOR_H
