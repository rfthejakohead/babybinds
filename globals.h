#ifndef BABYBINDS_GLOBALS_H
#define BABYBINDS_GLOBALS_H

/* For datatypes */
#include "datatypes.h"

/***** Compile time settings *****/
#ifndef BABYBINDS_COMBOBUFFER_SIZE
    #define BABYBINDS_COMBOBUFFER_SIZE 5
#endif

/***** Global variables *****/
/* These need to be global so that they are accessible within shutdownDaemon(), main.c, etc
   File descriptor for input device */
int devFD;

struct keyCombo* comboBinds;

struct keyExec* comboExecs;

/* The size of comboBinds AND comboExecs */
size_t bindNum;

#endif
