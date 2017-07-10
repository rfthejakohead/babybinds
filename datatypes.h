#ifndef BABYBINDS_DATATYPES_H
#define BABYBINDS_DATATYPES_H

/***** Data types for babybinds *****/
/*** For standard data types ***/
#include <stdlib.h>

/*** Key combo structs ***/

/* The struct array containing all key combinations
   The index is used to associate with its shell execute */
struct keyCombo {
    /* Array containing actual keycode sequence */
    int* codes;
    /* Size of array */
    size_t size;
};

/* Default value for keyCombo */
static const struct keyCombo defaultKeyCombo = { NULL, 0 };

/* The struct array containing all shell executes in the argv format
   Each arg is null terminated so its size is not saved (strlen to get length) */
struct keyExec {
    /* String containing all members serialized and separated by nulls */
    char* data;
    /* Pointer to each beginning of member in data array */
    char** elems;
    /* Size of elem array */
    size_t size;
};

/* Default value for keyExec */
static const struct keyExec defaultKeyExec = { NULL, NULL, 0 };

/*** Flag enums ***/
/* Read modes for parsing config file
   Note that the RM_ prefix obviously stands for Read Mode (RM) */
enum readMode {
    RM_starting, /* Starting mode, to determine if in a comment line or not                */
    RM_keycode,  /* Keycode  mode, read next data as an integer an keycode number          */
    RM_command,  /* Command  mode, read next data as an escapable string for shell command */
    RM_escape,   /* Escape   mode, escape next character                                   */
    RM_comment,  /* Comment  mode, ignore everything until next newline                    */
    RM_error     /* Error    mode, panic                                                   */
};

/* Unserialize flags for unserializing raw shell command data
   Note that the USM_ prefix (not so obviously) stands for UnSerialize Mode (USM) */
enum unserializeMode {
    USM_seek,     /* Seek      mode, waits for next null or end of string to trigger push or terminate modes */
    USM_push,     /* Push      mode, add next position as start for next element                             */
    USM_terminate /* Terminate mode, null terminate the ARRAY, NOT STRING                                    */
};

/* Error-level flags for taggedMsg(). Can be mixed together:
 * - First 2 bits represent the error level of the message.
 * - Next bit represent wether to flush or not afther the message is printed.
 * - Last bit adds a newline at the end
 */
enum tagErrorLevel {
    TM_info    = 0, /* Info    tag, represents a non-error. Just for information                             */
    TM_warning = 1, /* Warning tag, represents a non-critical error. Execution is supposed to continue       */
    TM_error   = 2, /* Error   tag, represents a critical error. Execution is supposed to halt               */
    TM_level   = 3, /* NOT A TAG! This is used to AND with a combined tag to get the error level             */
    TM_flush   = 4, /* Flush   tag, flushes the stream after the message is printed                          */
    TM_newline = 8  /* Newline tag, adds a newline at the end of the message, so that taggedMsg2 is not used */
};

#endif
