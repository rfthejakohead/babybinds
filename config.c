/***** config.h implementation *****/
#include "config.h"

int addKeybind(int* keycodes, size_t keycodesSize, char* exec, size_t execSize) {
    /*** Basic variable set-up ***/
    /* Declare thisNum for convenience and increment bind counter */
    const size_t thisNum = bindNum++;
    
    /* Other loop iterators and variables */
    size_t n;
    enum unserializeMode addNext;
    char* c;
    
    /*** Allocate main arrays ***/
    /* Allocate space for both structs and set their default values. Return 0 on failure */
  
    comboBinds = salloc(comboBinds, sizeof(struct keyCombo) * bindNum);
    if(salloc_f())
        return 0; /* Out of memory! */

    comboBinds[thisNum] = defaultKeyCombo;

    comboExecs = salloc(comboExecs, sizeof(struct keyExec) * bindNum);
    if(salloc_f())
        return 0; /* Out of memory! */

    comboExecs[thisNum] = defaultKeyExec;

    /*** Set actual values to comboBinds ***/
    /* Allocate space for the codes array. Return 0 on failure */
    comboBinds[thisNum].codes = salloc(NULL, sizeof(int) * keycodesSize);
    if(salloc_f())
        return 0; /* Out of memory! */

    /* Update size */
    comboBinds[thisNum].size = keycodesSize;

    /* Update keycodes */
    for(n = 0; n < keycodesSize; ++n)
        comboBinds[thisNum].codes[n] = keycodes[n];

    /*** Set actual values to comboExecs ***/
    /* Allocate space for the data array. Return 0 on failure */
    comboExecs[thisNum].data = salloc(NULL, execSize + 1);
    if(salloc_f())
        return 0; /* Out of memory! */

    /* Copy data using memcpy and append null terminator */
    memcpy(comboExecs[thisNum].data, exec, execSize);
    comboExecs[thisNum].data[execSize] = '\0';

    /* Increment size and allocate space for the first member. Return 0 on failure */
    comboExecs[thisNum].size = 1;
    comboExecs[thisNum].elems = salloc(NULL, sizeof(char*));
    if(salloc_f())
        return 0; /* Out of memory! */

    /* Set first element pointer to first data position */
    comboExecs[thisNum].elems[0] = comboExecs[thisNum].data;

    /* Find other elements in raw data
       Add next as element flags:
       -=-=-=-=-=-=-=-=-=-=-=-=-=-
       USM_seek     : Don't add yet, wait for next null or end of string
       USM_push     : Do add element to array, normally
       USM_terminate: Null terminate the ARRAY, NOT STRING */
    addNext = USM_seek;
    for(c = comboExecs[thisNum].data; c <= (comboExecs[thisNum].data + execSize); ++c) {
        /* Add element if flag is true or at the end of the array (for null terminator) */
        if(c == (comboExecs[thisNum].data + execSize))
            addNext = USM_terminate;

        if(addNext != USM_seek) {
            /* Increment size */
            ++comboExecs[thisNum].size;

            /* Expand element array. Return 0 on failure */
            comboExecs[thisNum].elems = salloc(comboExecs[thisNum].elems, sizeof(char*) * comboExecs[thisNum].size);
            if(salloc_f())
                return 0; /* Out of memory! */

            if(addNext == USM_terminate) {
                /* Null terminate array */
                comboExecs[thisNum].elems[comboExecs[thisNum].size - 1] = NULL;
                break;
            }
            else {
                /* Add new element normally */
                comboExecs[thisNum].elems[comboExecs[thisNum].size - 1] = c;
                addNext = USM_seek;
            }
        }

        /* Nulls indicate the end of an element (and the beginning of another, therefore) */
        if(*c == '\0')
            addNext = USM_push;
    }

    /* All (finally) done! */
    return 1;
}

void loadConfig(void) {
    /* Paths and files */
    char* homePath;
    char* configPath;
    FILE* configFP;

    /* Readmode for parsing */
    enum readMode mode;

    /* Current character */
    int c;

    /* Expandable databuffer variables */
    size_t databufSize;
    size_t databufI;
    char* databuf;

    /* Buffer for parsed keycode combos (array and iterator, no size as it uses BABYBINDS_COMBOBUFFER_SIZE) */
    int parsedCombos[BABYBINDS_COMBOBUFFER_SIZE];
    size_t parsedCombosI;

    /* Pre-computed array of positive powers of 10 (for str to positive int convertion). Max is 10 ^ 7 (8 digit keycode) */
    static const int pow10[8] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000 };

    /* Get configuration file path
       First, get the home path */
    homePath = getenv("HOME");
    if(homePath == NULL) {
        taggedMsg(TM_error | TM_flush | TM_newline, "Could not get home path! Aborting...");
        exit(EXIT_FAILURE);
        /* No need for graceful shutdown, as no I/O or dynamic memory outside this function has been fidled with
           This applies for every exit in this function outside the parse loop */
    }

    /* Then, allocate space for the variable (size of home path + size of /.babybindsrc (13) + null-terminator size (1) */
    configPath = salloc(NULL, strlen(getenv("HOME")) + 14);
    if(salloc_f())
        exit(EXIT_FAILURE);

    /* Then, append home path to it */
    strcpy(configPath, homePath);

    /* Finally, append the config file name to the end */
    strcat(configPath, "/.babybindsrc");

    /* Open configuration file */
    configFP = fopen(configPath, "r");
    if(configFP == NULL) {
        taggedMsg2(TM_error | TM_flush | TM_newline, "~/.babybindsrc could not be opened: ", strerror(errno));
        sfree(configPath);
        exit(EXIT_FAILURE);
    }

    /* Prepare variables for parsing
       Read modes:
       -=-=-=-=-=-
       RM_starting: Starting (after a newline, will check if the first char is a # for going into escape mode)
       RM_keycode : Keycode
       RM_command : Shell command
       RM_escape  : Escape next character (shell command mode)
       RM_comment : Comment (ignores everything until a newline)
       RM_error   : Error */
    mode = RM_starting;
    /* Buffer for non-parsed data (size, iterator and the actual buffer, respectively) or the shell script string itself */
    databufSize = 64;
    databufI = 0;
    databuf = salloc(NULL, databufSize);
    if(salloc_f()) {
        sfree(configPath);
        fclose(configFP);
        exit(EXIT_FAILURE);
    }
    
    /* Initialize combo array stuff */
    parsedCombosI = 0;

    /* Start parsing */
    do {
        /* Update character */
        c = fgetc(configFP);

        /* Ignore spaces and tabs unless in command mode */
        if(mode != RM_command && mode != RM_escape && (c == ' ' || c == '\t'))
            continue;

        /* If in starting mode, check for comment */
        if(mode == RM_starting) {
            if(c == '#')
                mode = RM_comment;
        }

        /* Check for newlines and EOF to save the parsed data */
        if(c == '\n' || c == EOF) {
            if(mode == RM_keycode) {
                taggedMsg(TM_error | TM_flush | TM_newline, "Malformed configuration file: Incomplete keybind (missing shell action)");
                mode = RM_error;
                break;
            }
            else if(mode == RM_command || mode == RM_escape) {
                /* Push data */
                if(!addKeybind(parsedCombos, parsedCombosI, databuf, databufI)) {
                    mode = RM_error;
                    break;
                }

                /* "Clear" the buffers */
                databufI = 0;
                parsedCombosI = 0;
            }

            /* Return to starting mode */
            mode = RM_starting;
        }
        else if(mode != RM_comment) {
            /* Add stuff to buffer if not in comment mode
               ... but first, enter keycode mode if in starting mode */
            if(mode == RM_starting)
                mode = RM_keycode;

            /* Parse data in buffer if switching mode */
            if((c == ';' || c == ':') && mode != RM_command && mode != RM_escape) {
                int parsedInt;
                size_t n;
                
                /* Field is empty (it can't be) */
                if(databufI == 0) {
                    taggedMsg(TM_error | TM_flush | TM_newline, "Malformed configuration file: Empty field");
                    mode = RM_error;
                    break;
                }

                /* Keycode too big (bigger than 8 chars) */
                if(databufI >= 8) {
                    taggedMsg(TM_error | TM_flush | TM_newline, "Keycode is ridiculously big");
                    mode = RM_error;
                    break;
                }

                /* Too many keycodes in combo (equal to BABYBINDS_COMBOBUFFER_SIZE) */
                if(parsedCombosI == BABYBINDS_COMBOBUFFER_SIZE) {
                    taggedMsg(TM_error | TM_flush | TM_newline, "Key combo has too many keycodes");
                    mode = RM_error;
                    break;
                }
                
                /* Switch to command mode, else, keep in keycode mode */
                if(c == ':')
                    mode = RM_command;

                /* Convert keycode string to keycode integer (positive only) */
                parsedInt = 0;
                for(n = 0; n < databufI; ++n) {
                    /* If in the valid range of characters, convert */
                    if(databuf[n] >= '0' && databuf[n] <= '9') {
                        /* Example convertion:
                           21, size 2, from left to right
                           2 * 10 ^ (size [2] - n [0] - 1) = 2 * 10 ^ 1 = 2 * 10 = 20
                           1 * 10 ^ (size [2] - n [1] - 1) = 1 * 10 ^ 0 = 1 * 1  =  1+
                                                                                  ----
                                                                                   21 */
                        parsedInt += (databuf[n] - '0') * pow10[databufI - n - 1];
                    }
                    else {
                        /* If not, error */
                        taggedMsg(TM_error | TM_flush | TM_newline, "Malformed configuration file: keycode is not a positive integer; invalid character");
                        mode = RM_error;
                        break;
                    }
                }

                /* Push data to temporary key combo buffer */
                parsedCombos[parsedCombosI++] = parsedInt;
                
                /* "Clear" the buffer */
                databufI = 0;
            }
            else {
                /* Expand the buffer to 2x its size if needed */
                if(databufI == databufSize) {
                    databufSize *= 2;
                    databuf = salloc(databuf, databufSize);
                    if(salloc_f()) {
                        mode = RM_error;
                        break;
                    }
                }

                if(mode == RM_keycode) { /* Keycode mode: just insert */
                    /* Append data to buffer */
                    databuf[databufI++] = c;
                }
                else if(mode == RM_command) { /* Command mode: check for backslashes, spaces and tabs insert accordingly */
                    if(c == '\\') {
                        databuf[databufI++] = '\\'; /* Insert backslash anyway. Explanation: */
                        /* This is done because it is guaranteed that an escape sequence inserts at least one character. The escaped character will replace this one.
                           It also avoids a repetition of the above buffer expansion in case of inserting 2 characters (happens on an invalid escape) avoiding problems
                           Furthermore, in case of having an EOF after the backslash, it is guaranteed that the backslash is inserted (counts as an invalid escape) */
                        
                        mode = RM_escape; /* Go to escape sequence mode */
                    }
                    else if(c == ' ' || c == '\t') {
                        /* Insert null if there was not a previous separator */
                        if(databufI == 0 || databuf[databufI - 1] != '\0')
                            databuf[databufI++] = '\0'; /* Insert null to represent new argument */
                    }
                    else
                        databuf[databufI++] = c; /* Just insert */
                }
                else if(mode == RM_escape && c != '\\') { /* Escaped script mode: parse escape sequences */
                    /* Note that backslashes are skipped because they are pre-inserted as a placeholder */
                    if(c == ' ' || c == '\t')
                        databuf[databufI - 1] = c; /* Escape the space/tab by replacing the previous placeholder backslash */
                    else if(c == 'n')
                        databuf[databufI - 1] = '\n'; /* Escape newline by replacing previous placeholder backslash */
                    else
                        databuf[databufI++] = c; /* Invalid escape sequence! Treat as a non escape sequence by inserting new char normally */

                    /* Go back to command mode */
                    mode = RM_command;
                }
            }
        }
    } while(c != EOF); /* Note: although we want to stop on EOF, we still want to update the shell script of the last bind, so we want to parse EOFs too */

    /* Clean-up this mess */
    fclose(configFP);
    sfree(configPath);
    sfree(databuf);
    if(mode == RM_error) {
        shutdownDaemon();
        exit(EXIT_FAILURE);
    }
}

