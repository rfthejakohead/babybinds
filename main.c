//// Includes
// General includes
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// General system calls includes
#include <unistd.h>
#include <errno.h>

// Signal handling includes
#include <signal.h>

// Linux input includes
#include <fcntl.h>
#include <linux/input.h>

//// TODO list:
// - Check the rest of the source (todos scattered all over it :| ) In a nutshell:
//   - Multi-line shell execs (implement escape sequences for newlines and backslahes)
//   - Arguments:
//     - Daemon mode
//     - Keycode check mode
//     - Non-default config file
//     - Multiple input devices
//     - Verbose flag (always on for now)
//   - Non-blocking shell executes (replacement for system())

//// Global variables
// These need to be global so that they are accessible within shutdownDaemon()
// File descriptor for input device
int devFD = -1;

// The array containing all key combinations
// The index is used to associate with its shell execute
int** comboBinds = NULL;
// The size of each element in comboBinds
size_t* comboBindsSubsizes = NULL;

// The array containing all shell executes
// The shell executes are null terminated so their size is not saved (strlen to get length)
char** comboExecs = NULL;

// The size of comboBinds AND comboExecs
size_t bindNum = 0;

//// Functions
// Gracefully shuts down (closes all I/O and frees memory)
void shutdownDaemon() {
    if(devFD > -1)
        close(devFD);
    for(size_t n = 0; n < bindNum; ++n) {
        if(comboBinds != NULL)
            free(comboBinds[n]);
        if(comboExecs != NULL)
            free(comboExecs[n]);
    }
    free(comboBinds);
    free(comboBindsSubsizes);
    free(comboExecs);
}

// Catches SIGINT to shut down
void handleSignal(int signum) {
    if(signum == SIGINT) {
        shutdownDaemon();
        fprintf(stderr, "[INFO] Interrupt caught! Shutting down gracefully...\n");
        exit(EXIT_SUCCESS);
    }
}

// Prints out of memory error message
void printMemoryErr() {
    fprintf(stderr, "[ERROR] Out of memory!\n");
}

// Prints program usage
void printUsage(const char* binName) {
    printf("Usage:\n");
    printf("%s <input device path>\n", binName);
}

// Inserts a key to the comparison buffer
// When inserting, an insertion sort is performed for easy keybind comparison (from smallest to biggest) and the new size is updated
void insertKey(int* comboBuffer, const size_t comboBufferSize, size_t* comboBufferN, int keycode) {
    if(*comboBufferN == 0) { // Special case: if comboBuffer is empty (n of 0) just insert the keycode
        *comboBufferN = 1;
        comboBuffer[0] = keycode;
        return;
    }

    // If the comboBuffer is full, ignore all other key presses
    if(*comboBufferN == comboBufferSize) {
        fprintf(stderr, "[INFO] Too many keys at the same time! Ignoring latest key...\n");
        return;
    }

    // Get insert position using a (kind of) insertion sort
    size_t i = 0;
    for(; i < *comboBufferN; ++i) {
        if(keycode < comboBuffer[i]) // Keycode has a smaller value than this element, position it here
            break;
        else if(keycode == comboBuffer[i]) { // Keycode is equal to this one! Ignore this insert as it is already in comboBuffer
            fprintf(stderr, "[INFO] Ignoring key (already in combo buffer)...\n");
            return;
        }
    }
    
    // Shift all elements after the position of the new keycode to the right for ordering
    for(size_t si = *comboBufferN - 1; si >= i; --si) {
        comboBuffer[si + 1] = comboBuffer[si];
        if(si == 0)
            break; // Prevents segfault due to underflow
    }

    // Add keycode to resulting position
    comboBuffer[i] = keycode;

    // Update comboBufferN
    ++(*comboBufferN);
}

// Removes a key from the comparison buffer
// Everything is pushed back to line up and the new size is updated. If a key couldn't be removed (not in buffer) do nothing, as it might have been ignored by insertKey
void removeKey(int* comboBuffer, const size_t comboBufferSize, size_t* comboBufferN, int keycode) {
    // Get position of keycode
    size_t i = 0;
    for(; i < *comboBufferN; ++i) {
        if(comboBuffer[i] == keycode)
            break;
    }

    // If the position is equal to comboBufferN, abort, as it means the keycode was not found in the comboBuffer
    if(i == *comboBufferN)
        return;

    // Update comboBufferN
    --(*comboBufferN);

    // Remove the keycode from the comboBuffer by shifting all values left
    for(size_t si = i; si < *comboBufferN; ++si)
        comboBuffer[si] = comboBuffer[si + 1];
}

// TODO: Non-blocking system() for doSingleBind() and doBind()
// Like doBind but for a single key
void doSingleBind(int keycode) {
    // Iterate over all keybinds
    for(size_t i = 0; i < bindNum; ++i) {
        // Single sized and same keycode?
        if(comboBindsSubsizes[i] == 1 && comboBinds[i][0] == keycode) {
            // Trigger keybind!
            printf("Single bind triggered: %s\n", comboExecs[i]);
            fflush(stdout);
            system(comboExecs[i]);
            // All done! Return...
            return;
        }
    }
}

// Checks if there is any keybind with this key combination and do what the bind wants - NON-BLOCKING
void doBind(int* comboBuffer, size_t comboBufferN) {
    // Iterate over all keybinds
    for(size_t i = 0; i < bindNum; ++i) {
        // Same size?
        if(comboBindsSubsizes[i] == comboBufferN) {
            // Same keycodes?
            size_t n = 0;
            for(; n < comboBufferN; ++n) {
                if(comboBinds[i][n] != comboBuffer[n])
                    break; // Different keycode! Stop looping...
            }
            // If the loop didnt reach the end, then the keycodes didnt match. Skip this combo
            if(n != comboBufferN)
                continue;

            // Yes! Trigger keybind!
            printf("Multi-key bind triggered: %s\n", comboExecs[i]);
            fflush(stdout);
            system(comboExecs[i]);
            // All done! Return...
            return;
        }
    }
}

// Adds a keybind to memory
// Note that exec is NOT null terminated! That is why execSize is needed
int addKeybind(int* keycodes, size_t keycodesSize, char* exec, size_t execSize) {
    ++bindNum;
    // Allocate space for bind array
    if(comboBinds == NULL) { // First time. If comboBinds is in its first time, so is comboExecs and comboBindsSubsizes
        comboBinds = malloc(sizeof(int*));
        comboBindsSubsizes = malloc(sizeof(size_t));
        comboExecs = malloc(sizeof(char*));
    }
    else { // Not first time
        comboBinds = realloc(comboBinds, bindNum * sizeof(int*));
        comboBindsSubsizes = realloc(comboBindsSubsizes, bindNum * sizeof(size_t));
        comboExecs = realloc(comboExecs, bindNum * sizeof(char*));
    }
    
    // Out of memory?
    if((comboBinds == NULL) || (comboBindsSubsizes == NULL) || (comboExecs == NULL)) {
        printMemoryErr();
        return 0;
    }

    // Set new elements to null, just in case we run out of memory
    comboBinds[bindNum - 1] = NULL;
    comboExecs[bindNum - 1] = NULL;

    // Update subsizes
    comboBindsSubsizes[bindNum - 1] = keycodesSize;
    
    // Allocate memory for comboBinds sub-array
    comboBinds[bindNum - 1] = malloc(keycodesSize * sizeof(int));

    // Out of memory?
    if(comboBinds[bindNum - 1] == NULL) {
        printMemoryErr();
        return 0;
    }

    // Set the values using memcpy
    memcpy(comboBinds[bindNum - 1], keycodes, keycodesSize * sizeof(int));

    // Allocate memory for comboExecs sub-array
    comboExecs[bindNum - 1] = malloc(execSize + 1);

    // Out of memory?
    if(comboExecs[bindNum - 1] == NULL) {
        printMemoryErr();
        return 0;
    }

    // Set value using memcpy
    memcpy(comboExecs[bindNum - 1], exec, execSize);

    // Append null character
    comboExecs[bindNum - 1][execSize] = '\0';

    // All done! (finally)
    return 1;
}

// Loads ~/.babybindsrc, which contains all keybinds
// # indicate comments (like in shell scripts)
// All spaces, tabs, comments and empty lines are ignored
// Format is:
//   <keycode (int)>;<keycode>;<...>:<shell script (string)>
// Notes: 
// - the last separator is a colon, not a semicolon
// - only the first colon indicates the end of keycodes, all other syntax followed counts as the shell code
// - the shell code may not contain newlines. TODO: character escape sequences for newlines and (consequently) backslashes (maybe others in the future, when needed)
// - there may be as many keycodes as possible, but they will be ignored if more than maxComboSize, discarding the whole combo
void loadConfig(const size_t maxComboSize) {
    // Get configuration file path
    // First, get the home path
    char* homePath = getenv("HOME");
    if(homePath == NULL) {
        fprintf(stderr, "[ERROR] Could not get home path! Aborting...\n");
        exit(EXIT_FAILURE);
        // No need for graceful shutdown, as no I/O or dynamic memory outside this function has been fidled with
        // This applies for every exit in this function outside the parse loop
    }

    // Then, allocate space for the variable (size of home path + size of /.babybindsrc (13) + null-terminator size (1)
    char* configPath = malloc(strlen(getenv("HOME")) + 14);
    if(configPath == NULL) {
        printMemoryErr();
        exit(EXIT_FAILURE);
    }

    // Then, append home path to it
    strcpy(configPath, homePath);

    // Finally, append the config file name to the end
    strcat(configPath, "/.babybindsrc");

    // Open configuration file
    FILE* configFP = fopen(configPath, "r");
    if(configFP == NULL) {
        fprintf(stderr, "[ERROR] ~/.babybindsrc could not be opened: %s\n", strerror(errno));
        free(configPath);
        exit(EXIT_FAILURE);
    }

    // Prepare variables for parsing
    // Mode of read:
    // 0: Starting (after a newline, will check if the first char is a # for going into mode 3)
    // 1: Keycode
    // 2: Shell script
    // 3: Comment (ignores everything until a newline)
    // 4: Error
    char mode = 0;
    // Current "character"
    int c;
    // Buffer for non-parsed data (size, iterator and the actual buffer, respectively) or the shell script string itself
    size_t databufSize = 64;
    size_t databufI = 0;
    char* databuf = malloc(databufSize);
    if(databuf == NULL) {
        printMemoryErr();
        free(configPath);
        fclose(configFP);
        exit(EXIT_FAILURE);
    }
    // Buffer for parsed keycode combos (array and iterator, no size as it uses maxComboSize)
    int parsedCombos[maxComboSize];
    size_t parsedCombosI = 0;
    // Pre-computed array of positive powers of 10 (for str to positive int convertion). Max is 10 ^ 7 (8 digit keycode)
    int pow10[8] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000 };

    // Start parsing
    do {
        // Update character
        c = fgetc(configFP);

        // Ignore spaces and tabs unless in shell script mode
        if(mode != 2 && (c == ' ' || c == '\t'))
            continue;

        // If in starting mode, check for comment
        if(mode == 0) {
            if(c == '#')
                mode = 3;
        }

        // Check for newlines and EOF to save the parsed data
        if(c == '\n' || c == EOF) {
            if(mode == 1) {
                fprintf(stderr, "[ERROR] Malformed configuration file: Incomplete keybind (missing shell action)\n");
                mode = 4;
                break;
            }
            else if(mode == 2) {
                // Push data
                if(!addKeybind(parsedCombos, parsedCombosI, databuf, databufI)) {
                    mode = 4;
                    break;
                }

                // "Clear" the buffers
                databufI = 0;
                parsedCombosI = 0;
            }

            // Return to starting mode
            mode = 0;
        }
        else if(mode != 3) {
            // Add stuff to buffer if not in comment mode
            // ... but first, enter keycode mode if in starting mode
            if(mode == 0)
                mode = 1;

            // Parse data in buffer if switching mode
            if((c == ';' || c == ':') && mode != 2) {
                // Field is empty (it can't be)
                if(databufI == 0) {
                    fprintf(stderr, "[ERROR] Malformed configuration file: Empty field\n");
                    mode = 4;
                    break;
                }

                // Keycode too big (bigger than 8 chars)
                if(databufI >= 8) {
                    fprintf(stderr, "[ERROR] Keycode is ridiculously big (%zu characters long!)\n", databufI);
                    mode = 4;
                    break;
                }

                // Too many keycodes in combo (equal to maxComboSize)
                if(parsedCombosI == maxComboSize) {
                    fprintf(stderr, "[ERROR] Key combo has too many keycodes (%zu)\n", maxComboSize);
                    mode = 4;
                    break;
                }
                
                // Switch to mode 2, else, keep in mode 1
                if(c == ':')
                    mode = 2;

                // Convert keycode string to keycode integer (positive only)
                int parsedInt = 0;
                for(size_t n = 0; n < databufI; ++n) {
                    // If in the valid range of characters, convert
                    if(databuf[n] >= '0' && databuf[n] <= '9') {
                        // Example convertion:
                        // 21, size 2, from left to right
                        // 2 * 10 ^ (size [2] - n [0] - 1) = 2 * 10 ^ 1 = 2 * 10 = 20
                        // 1 * 10 ^ (size [2] - n [1] - 1) = 1 * 10 ^ 0 = 1 * 1  =  1+
                        //                                                        ----
                        //                                                         21
                        parsedInt += (databuf[n] - '0') * pow10[databufI - n - 1];
                    }
                    else {
                        // If not, error
                        fprintf(stderr, "[ERROR] Malformed configuration file: keycode is not a positive integer; invalid character: %c\n", databuf[n]);
                        mode = 4;
                        break;
                    }
                }

                // Push data to temporary key combo buffer
                parsedCombos[parsedCombosI++] = parsedInt;
                
                // "Clear" the buffer
                databufI = 0;
            }
            else {
                // Expand the buffer to 2x its size if needed
                if(databufI == databufSize) {
                    databufSize *= 2;
                    databuf = realloc(databuf, databufSize);
                    if(databuf == NULL) {
                        printMemoryErr();
                        mode = 4;
                        break;
                    }
                }

                // Append data to buffer
                databuf[databufI++] = c;
            }
        }
    } while(c != EOF); // Note: although we want to stop on EOF, we still want to update the shell script of the last bind, so we want to parse EOFs too

    // Clean-up this mess
    fclose(configFP);
    free(configPath);
    free(databuf);
    if(mode == 4) {
        shutdownDaemon();
        exit(EXIT_FAILURE);
    }
}

// Main (contains keybind loop)
int main(int argc, char* argv[]) {
    //// Prepare variables (part 1)
    // Max of 5 keys combo (who even does a 5 key combo???)
    const size_t comboBufferSize = 5;
    
    //// Load config
    loadConfig(comboBufferSize);

    //// Initialize signal handler
    if(signal(SIGINT, handleSignal) == SIG_ERR) {
        fprintf(stderr, "[ERROR] Cannot catch interrupt signals, so running is risky! Aborting...\n");
        return EXIT_FAILURE;
    }

    //// Parse arguments
    // TODO: verbose flag (always verbose for now), multiple devs (*), non-default .*rc, combo code check mode, daemon (*)
    if(argc > 1) {
        // Open the input device
        devFD = open(argv[1], O_RDONLY);
        if(devFD <= -1) {
            fprintf(stderr, "[ERROR] Could not open input device %s: %s\n", argv[1], strerror(errno));
            return EXIT_FAILURE;
        }
    }
    else {
        fprintf(stderr, "[ERROR] No input devices passed!\n");
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    //// Prepare variables (part 2)
    // Event
    struct input_event ev;
    // Error (or success) of read
    ssize_t n;
    // Key combo buffer
    size_t comboBufferN = 0;
    int comboBuffer[comboBufferSize];

    //// Wait for keys and parse them
    printf("[INFO] Started! Interrupt to exit.\n");
    fflush(stdout);

    while(1) {
        n = read(devFD, &ev, sizeof(ev));
        if(n == sizeof(ev)) {
            // Read was successful! Parse stuff and check if it is a keybind
            if(ev.type == EV_KEY) { // Input is a key! Continue...
                // Notes:
                // - key autorepeats are ignored as we don't need to care about them for key combinations
                // - single-key keybinds are triggered on key release and ONLY IF ALONE
                // - multi-key keybinds are triggered on key press
                // - key combos are ordered by ev.code value

                if(ev.value == 0) { // Key released
                    removeKey(comboBuffer, comboBufferSize, &comboBufferN, ev.code);
                    if(comboBufferN == 0)
                        doSingleBind(ev.code);
                }
                else if(ev.value == 1) { // Key pressed
                    insertKey(comboBuffer, comboBufferSize, &comboBufferN, ev.code);
                    if(comboBufferN > 1)
                        doBind(comboBuffer, comboBufferN);
                }
            }
        }
        else {
            // Read errored! Skip this iteration.
            fprintf(stderr, "[ERROR] Input device read failed! Ignoring...\n");
            fflush(stdout);
            break;
        }
    }

    //// Clean-up
    shutdownDaemon();

    return EXIT_SUCCESS;
}
