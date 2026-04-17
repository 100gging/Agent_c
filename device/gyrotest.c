#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#define MPU6050_I2C_ADDR   0x68

#define REG_SMPLRT_DIV     0x19
#define REG_CONFIG         0x1A
#define REG_GYRO_CONFIG    0x1B
#define REG_PWR_MGMT_1     0x6B
#define REG_WHO_AM_I       0x75

#define REG_GYRO_XOUT_H    0x43

static int file = -1;

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void i2c_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };

    if (write(file, buf, 2) != 2)
        die("i2c_write failed");
}

static uint8_t i2c_read_byte(uint8_t reg)
{
    uint8_t buf = reg;

    if (write(file, &buf, 1) != 1)
        die("i2c_read_byte: write reg failed");

    if (read(file, &buf, 1) != 1)
        die("i2c_read_byte: read failed");

    return buf;
}

static void i2c_read_block(uint8_t start_reg, uint8_t *buf, size_t len)
{
    uint8_t reg = start_reg;

    if (write(file, &reg, 1) != 1)
        die("i2c_read_block: write reg failed");

    if (read(file, buf, len) != (ssize_t)len)
        die("i2c_read_block: read block failed");
}

static int16_t be16_to_int16(uint8_t high, uint8_t low)
{
    return (int16_t)(((uint16_t)high << 8) | low);
}

int main(void)
{
    uint8_t who_am_i;
    uint8_t data[6];

    file = open("/dev/i2c-1", O_RDWR);
    if (file < 0)
        die("open /dev/i2c-1 failed");

    if (ioctl(file, I2C_SLAVE, MPU6050_I2C_ADDR) < 0)
        die("ioctl I2C_SLAVE failed");

    who_am_i = i2c_read_byte(REG_WHO_AM_I);
    printf("WHO_AM_I = 0x%02X\n", who_am_i);

    if (who_am_i != 0x68) {
        fprintf(stderr, "Unexpected WHO_AM_I value\n");
        return 1;
    }

    /* wake up */
    i2c_write(REG_PWR_MGMT_1, 0x01);
    usleep(100000);

    /* sample rate, dlpf, gyro full scale = ±250 dps */
    i2c_write(REG_SMPLRT_DIV, 0x07);
    i2c_write(REG_CONFIG, 0x00);
    i2c_write(REG_GYRO_CONFIG, 0x00);

    printf("=============================================================\n");
    printf(" GX(raw)   GY(raw)   GZ(raw)   |   GX(dps)   GY(dps)   GZ(dps)\n");
    printf("=============================================================\n");

    while (1) {
        int16_t gx_raw, gy_raw, gz_raw;
        float gx_dps, gy_dps, gz_dps;

        i2c_read_block(REG_GYRO_XOUT_H, data, 6);

        gx_raw = be16_to_int16(data[0], data[1]);
        gy_raw = be16_to_int16(data[2], data[3]);
        gz_raw = be16_to_int16(data[4], data[5]);

        /* ±250 dps -> 131 LSB/dps */
        gx_dps = gx_raw / 131.0f;
        gy_dps = gy_raw / 131.0f;
        gz_dps = gz_raw / 131.0f;

        printf("%8d  %8d  %8d   |  %8.2f  %8.2f  %8.2f\n",
               gx_raw, gy_raw, gz_raw,
               gx_dps, gy_dps, gz_dps);

        usleep(200000);
    }

    return 0;
}