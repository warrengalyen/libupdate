#include <stdio.h>

#include <update.h>

int main(void)
{
    if (update_init() != 0) {
        return 1;
    }

    puts("updater: stub run OK");

    update_shutdown();
    return 0;
}
