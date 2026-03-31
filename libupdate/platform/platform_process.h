#ifndef PLATFORM_PROCESS_H
#define PLATFORM_PROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Process / subprocess abstraction (declarations only; implementations are stubs).
 */
int platform_process_spawn_stub(void);
int platform_process_wait_stub(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_PROCESS_H */
