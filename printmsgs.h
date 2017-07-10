#ifndef BABYBINDS_PRINTMSGS_H
#define BABYBINDS_PRINTMSGS_H

/***** All stuff related to printing for babybinds *****/
/* For tagErrorLevel */
#include "datatypes.h"

/* Standard includes */
#include <stdio.h>

/* Prints 1/2 string(s) with a error-level tag before it */
void taggedMsg2(enum tagErrorLevel tags, const char* str1, char* str2);

/* Syntax friendly single string version of taggedMsg2 */
void taggedMsg(enum tagErrorLevel tags, const char* str);

/* Prints program usage */
void printUsage(const char* binName);

/* Prints parsed commands in a human-readable way. Args is a null terminated ARRAY (last element is a NULL pointer) */
void printCommand(char** args, size_t size);

#endif
