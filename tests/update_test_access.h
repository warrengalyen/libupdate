#ifndef UPDATE_TEST_ACCESS_H
#define UPDATE_TEST_ACCESS_H

#include <update.h>

#if defined(UPDATE_BUILD_TEST_HOOKS)
UPDATE_API const char *update_test_get_install_dir(void);
UPDATE_API const char *update_test_get_update_url(void);
UPDATE_API void update_test_reset_context(void);
#endif

#endif /* UPDATE_TEST_ACCESS_H */
