/*** Includes  ***/
/* This header should chain include all neccessary header files */
#include "config.h"

/* Linux input includes */
#include <fcntl.h>
#include <linux/input.h>

/*
 * Commits:
 * #1 (Hotfix):
 *  - Fixed unordered multi-key combos
 * #2 (Fixes, error handling, non-blocking shell executes and escape sequences):
 *  - Fixed bad reads not being ignored and exiting
 *  - Babybinds now waits 3 seconds on a bad read to decrease screen litter and aborts on the 10th failed try
 *  - Fixed multi-key keybinds not working if the combo in the config is not ordered from small to big
 *  - Fixed stdout being flushed instead of stderr when a failed read occurred
 *  - Replaced system with a non-blocking alternative using fork and execvp
 *    > Note that because of this, some extra parsing is done when loading a config to transform shell commands into an argv array
 *    > This also means that wildcards don't work! If you want to do a TRUE shell command, make a script or start a new shell
 *  - Removed pointless signal error handling, as the signums are all valid
 *  - Removed some pointless checks when handling SIGINTs
 *  - Keycode inserts and removes from arrays now use nice wrapper functions for this (intPtrRemove and intPtrOrderedUniqueInsert)
 *  - Added escape sequences to the config file's shell commands:
 *    > Escape character is the backslash (\)
 *    > Escapable characters are spaces, tabs, backslashes and newlines (not literal newlines, use n)
 *    > Invalid escape sequences are ignored and treated as two regular characters (like in bash)
 *    > Null characters cannot be escaped due to c-strings being null-terminated (I don't think any OS allows this anyway...)
 *  - Replaced some printf with *put* family to avoid having yourself accidentally hacking your own computer using format string attacks and to speed stuff up
 *    > Some haven't been replaced yet, however (too lazy)
 * #3 (Hotfix, corrected commit number):
 *  - Corrected commit numbers
 *  - Fixed escape sequences not working (poorly tested before this :(, sorry)
 * #4 (C89 and general organization)
 *  - Separated stuff in headers
 *  - No more VLAs
 *  - No more un-needed or dangerous printfs
 *  - Made code C89 conforming (I hate it already)
 *  - All memory management is now in wrapper functions
 *  - Code should compile with no warnings now
 */

/* TODO list:
 * - Check the rest of the source (todos scattered all over it :| ) In a nutshell:
 *   - Arguments:
 *     - Daemon mode
 *     - Keycode check mode
 *     - Non-default config file
 *     - Multiple input devices
 *     - Verbose flag (always on for now)
 */

/*** Functions ***/
/* Inserts a key to the comparison buffer
   When inserting, an insertion sort is performed for easy keybind comparison (from smallest to biggest) and the new size is updated */
void insertKey(int* comboBuffer, size_t* comboBufferN, int keycode);

/* Removes a key from the comparison buffer
   Everything is pushed back to line up and the new size is updated. If a key couldn't be removed (not in buffer) do nothing, as it might have been ignored by insertKey */
void removeKey(int* comboBuffer, size_t* comboBufferN, int keycode);

/*** Function implementations ***/
void insertKey(int* comboBuffer, size_t* comboBufferN, int keycode) {
    size_t newSize;

    /* If the comboBuffer is full, ignore all other key presses */
    if(*comboBufferN == BABYBINDS_COMBOBUFFER_SIZE) {
        taggedMsg(TM_info | TM_flush | TM_newline, "Too many keys at the same time! Ignoring latest key...");
        return;
    }

    /* Insert using already existing unique ordered array insert function */
    newSize = intPtrOrderedUniqueInsert(comboBuffer, *comboBufferN, keycode);
    /* If size remained the same, value was already in the array, so ignore it
       Note that this probably will never happen unless you have a keyboard with keys with equal keycodes (is there even a keyboard that does this? :S ) */
    if(newSize == *comboBufferN)
        taggedMsg(TM_info | TM_flush | TM_newline, "Ignoring key (already in combo buffer)...");
    else
        *comboBufferN = newSize;
}

void removeKey(int* comboBuffer, size_t* comboBufferN, int keycode) {
    /* Remove using already existing function */
    *comboBufferN = intPtrRemove(comboBuffer, *comboBufferN, keycode);
}

/* Main (contains keybind loop) */
int main(int argc, char* argv[]) {
    /*** Declare variables ***/
    /* Event */
    struct input_event ev;
    /* Error (or success) of read */
    ssize_t n;
    /* Key combo buffer */
    size_t comboBufferN;
    int comboBuffer[BABYBINDS_COMBOBUFFER_SIZE];
    /* Read fail counter */
    unsigned char failNum;

    /*** Initialize globals ***/
    devFD = -1;
    comboBinds = NULL;
    comboExecs = NULL;
    bindNum = 0;

    /*** Parse arguments ***/
    /* TODO: verbose flag (always verbose for now), multiple devs (*), non-default .*rc, combo code check mode, daemon (*) */
    if(argc > 1) {
        /* Open the input device */
        devFD = open(argv[1], O_RDONLY);
        if(devFD <= -1) {
            taggedMsg2(TM_error | TM_flush | TM_newline, "Could not open input device: ", strerror(errno));
            return EXIT_FAILURE;
        }
    }
    else {
        taggedMsg(TM_error | TM_flush | TM_newline, "No input devices passed!");
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    /*** Initialize variables ***/
    /* Prepare comboBuffer */
    comboBufferN = 0;

    /* Prepare other stuff */
    failNum = 0;
    
    /*** Load config ***/
    loadConfig();

    /*** Handle signals ***/
    /* On interrupt, use the interruptHandler function */
    signal(SIGINT, interruptHandler);

    /* Tell the kernel to automatically reap child processes (prevents defunct processes) */
    signal(SIGCHLD, SIG_IGN);

    /*** Wait for keys and parse them ***/
    taggedMsg(TM_info | TM_flush | TM_newline, "Started! Interrupt to exit.");

    while(1) {
        n = read(devFD, &ev, sizeof(ev));
        if(n == sizeof(ev)) {
            /* Read was successful! Parse stuff, reset fail counter and check if it is a keybind */
            failNum = 0;
            if(ev.type == EV_KEY) { /* Input is a key! Continue... */
                /* Notes:
                   - key autorepeats are ignored as we don't need to care about them for key combinations
                   - single-key keybinds are triggered on key release and ONLY IF ALONE
                   - multi-key keybinds are triggered on key press
                   - key combos are ordered by ev.code value */

                if(ev.value == 0) { /* Key released */
                    removeKey(comboBuffer, &comboBufferN, ev.code);
                    if(comboBufferN == 0)
                        doSingleBind(ev.code);
                }
                else if(ev.value == 1) { /* Key pressed */
                    insertKey(comboBuffer, &comboBufferN, ev.code);
                    if(comboBufferN > 1)
                        doBind(comboBuffer, comboBufferN);
                }
            }
        }
        else {
            /* Read errored! Skip this iteration, or abort, if too many failed reads. */
            if(failNum == 10) {
                taggedMsg(TM_error | TM_flush | TM_newline, "Input device read failed! Aborting (10 fails)...");
                break;
            }
            
            taggedMsg(TM_warning | TM_flush | TM_newline, "Input device read failed! Ignoring and waiting...");
            
            /* Wait 3 seconds */
            sleep(3);
            
            /* Increment fail counter */
            ++failNum;
        }
    }

    /*** Clean-up ***/
    shutdownDaemon();

    return EXIT_SUCCESS;
}
