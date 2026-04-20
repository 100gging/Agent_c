#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int ret;

    ret = system("cd /mnt/nfs && "
                 "export ALSA_CONFIG_PATH=/mnt/nfs/alsa-lib/share/alsa/alsa.conf && "
                 "export LD_LIBRARY_PATH=/mnt/nfs/alsa-lib/lib:/mnt/nfs/alsa-lib/lib/alsa-lib:/mnt/nfs/alsa-lib/lib/alsa-lib/smixer && "
                 "./aplay -Dhw:0,0 /mnt/nfs/test_contents/test.wav");

    if (ret == -1) {
        perror("system failed");
        return 1;
    }

    printf("aplay finished, return=%d\n", ret);
    return 0;
}