#ifndef BABYBINDS_CALL_H
#define BABYBINDS_CALL_H

/***** All stuff related to calling commands and clean-up *****/
/* For freeing globals */
#include "globals.h"

/* For memory management */
#include "memory.h"

/* For error messages */
#include "printmsgs.h"

/* For errno */
#include <errno.h>
#include <string.h>

/* For fork and exec */
#include <unistd.h>
#include <sys/types.h>

/* For signal handling */
#include <signal.h>

/* Gracefully shuts down (closes all I/O and frees memory) */
void shutdownDaemon(void);

/* Catches SIGINT to shut down */
void interruptHandler(int signum);

/* Executes a shell command in a non-blocking way */
void doShellExec(char** command);

/* Like doBind but for a single key */
void doSingleBind(int keycode);

/* Checks if there is any keybind with this key combination and do what the bind wants - NON-BLOCKING */
void doBind(int* comboBuffer, size_t comboBufferN);

#endif
