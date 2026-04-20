#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/*
 * Raspberry Pi 3 onboard switches:
 *   SW_2 -> BCM 17
 *   SW_3 -> BCM 18
 */

#define GPIO_CHIP       "/dev/gpiochip0"
#define SW2_BCM         17
#define SW3_BCM         18

int main(void)
{
    int chip_fd;
    struct gpiohandle_request req;
    struct gpiohandle_data data;

    chip_fd = open(GPIO_CHIP, O_RDONLY);
    if (chip_fd < 0) {
        perror("open " GPIO_CHIP);
        return 1;
    }

    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = SW2_BCM;   /* SW_2 */
    req.lineoffsets[1] = SW3_BCM;   /* SW_3 */
    req.flags = GPIOHANDLE_REQUEST_INPUT;
    req.lines = 2;
    strcpy(req.consumer_label, "sw2_sw3_test");

    if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL");
        close(chip_fd);
        return 1;
    }

    printf("Monitoring SW_2 (BCM17) and SW_3 (BCM18). Press Ctrl+C to exit.\n");

    while (1) {
        memset(&data, 0, sizeof(data));

        if (ioctl(req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
            perror("GPIOHANDLE_GET_LINE_VALUES_IOCTL");
            close(req.fd);
            close(chip_fd);
            return 1;
        }

        printf("SW_2 (BCM17) = %d  |  SW_3 (BCM18) = %d\n",
               data.values[0], data.values[1]);
        fflush(stdout);
        usleep(200000);
    }

    close(req.fd);
    close(chip_fd);
    return 0;
}
