#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    int chip_fd;
    struct gpiohandle_request req;
    struct gpiohandle_data data;

    chip_fd = open("/dev/gpiochip0", O_RDONLY);
    if (chip_fd < 0) {
        perror("open /dev/gpiochip0");
        return 1;
    }

    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = 4;   // BCM4
    req.flags = GPIOHANDLE_REQUEST_INPUT;
    req.lines = 1;
    strcpy(req.consumer_label, "button_test");

    if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL");
        close(chip_fd);
        return 1;
    }

    while (1) {
        memset(&data, 0, sizeof(data));

        if (ioctl(req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
            perror("GPIOHANDLE_GET_LINE_VALUES_IOCTL");
            close(req.fd);
            close(chip_fd);
            return 1;
        }

        printf("button = %d\n", data.values[0]);
        fflush(stdout);
        usleep(200000);
    }

    close(req.fd);
    close(chip_fd);
    return 0;
}