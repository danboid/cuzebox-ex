#ifndef LOGICAL_TEST_H
#define LOGICAL_TEST_H
#include "types.h"
/* Debug-only orchestration. No game-specific state interpretation. */
void logical_test_command(char *command, char *reply, size_t capacity);
void logical_test_stop(void);
void logical_test_reset(void);
boole logical_test_active(void);
#endif
