#include <stddef.h>

#include <update.h>

int main(void)
{
    const update_options_t opts = {
        "https://example.invalid/updates",
        "updater",
        NULL,
        NULL,
        NULL,
        NULL,
    };

    if (update_init(&opts) != UPDATE_OK) {
        return UPDATE_ERROR;
    }

    return update_perform();
}
