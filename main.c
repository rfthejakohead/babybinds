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

/*
 * Commits:
 * #1 (Fixes, error handling, non-blocking shell executes and escape sequences):
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
 */

//// TODO list:
// - Check the rest of the source (todos scattered all over it :| ) In a nutshell:
//   - Multi-line shell execs (implement escape sequences for newlines and backslahes)
//   - Arguments:
//     - Daemon mode
//     - Keycode check mode
//     - Non-default config file
//     - Multiple input devices
//     - Verbose flag (always on for now)
// - Replace all VLAs with malloc and free
// - Erradicate *printf
// - Use enums for flags
// - Use more structs where needed

//// Global variables
// These need to be global so that they are accessible within shutdownDaemon()
// File descriptor for input device
int devFD = -1;

// The struct array containing all key combinations
// The index is used to associate with its shell execute
struct keyCombo {
    // Array containing actual keycode sequence
    int* codes;
    // Size of array
    size_t size;
} defaultKeyCombo = { NULL, 0 };

struct keyCombo* comboBinds = NULL;

// The struct array containing all shell executes in the argv format
// Each arg is null terminated so its size is not saved (strlen to get length)
struct keyExec {
    // String containing all members serialized and separated by nulls
    char* data;
    // Pointer to each beginning of member in data array
    char** elems;
    // Size of elem array
    size_t size;
} defaultKeyExec = { NULL, NULL, 0 };

struct keyExec* comboExecs = NULL;

// The size of comboBinds AND comboExecs
size_t bindNum = 0;

//// Functions
// Gracefully shuts down (closes all I/O and frees memory)
void shutdownDaemon() {
    if(devFD > -1)
        close(devFD);
    for(size_t n = 0; n < bindNum; ++n) {
        if(comboBinds != NULL)
            free(comboBinds[n].codes);
        if(comboExecs != NULL) {
            free(comboExecs[n].elems);
            free(comboExecs[n].data);
        }
    }
    free(comboBinds);
    free(comboExecs);
}

// Catches SIGINT to shut down
void interruptHandler(int signum) {
    fputs("[INFO] Interrupt caught! Shutting down gracefully...\n", stderr);
    shutdownDaemon();
    exit(EXIT_SUCCESS);
}

// Prints out of memory error message
void printMemoryErr() {
    fputs("[ERROR] Out of memory!\n", stderr);
}

// Prints program usage
void printUsage(const char* binName) {
    printf("Usage:\n");
    printf("%s <input device path>\n", binName);
}

// Inserts an unique value (can only be one) to a fixed size int array using insertion sort
// Returns final size. If the size remains the same, an error occured
// Note that this function does not handle the check to see if the array will exceed its maximum size
size_t intPtrOrderedUniqueInsert(int* array, size_t size, int val) {
    // Special case: if the array is empty (size of 0) just insert the value
    if(size == 0) {
        array[0] = val;
        return 1;
    }

    // Get insert position using a (kind of) insertion sort
    size_t i = 0;
    for(; i < size; ++i) {
        if(val < array[i]) // Value is smaller than this element, position it here
            break;
        else if(val == array[i]) // Value is equal to this one! Ignore this insert as value is already in array and it must be unique
            return size;
    }
    
    // Shift all elements after the position of the new element to the right for ordering
    for(size_t si = size - 1; si >= i; --si) {
        array[si + 1] = array[si];
        if(si == 0)
            break; // Prevents segfault due to underflow
    }

    // Add value to resulting position
    array[i] = val;

    // Return new size
    return size + 1;
}

// Removes an element from a fixed size int array by value
// Returns final size. If the size remains the same, an error occured
size_t intPtrRemove(int* array, size_t size, int val) {
    // Get element associated with value
    size_t i = 0;
    for(; i < size; ++i) {
        if(array[i] == val)
            break;
    }

    // If the position is equal to size, abort, as it means the value was not found in the array
    if(i == size)
        return size;

    // Remove the element from the array by shifting all values left (last element's value not cleared!)
    for(; i < size - 1; ++i)
        array[i] = array[i + 1];

    // Return new size
    return size - 1;
}

// Inserts a key to the comparison buffer
// When inserting, an insertion sort is performed for easy keybind comparison (from smallest to biggest) and the new size is updated
void insertKey(int* comboBuffer, const size_t comboBufferSize, size_t* comboBufferN, int keycode) {
    // If the comboBuffer is full, ignore all other key presses
    if(*comboBufferN == comboBufferSize) {
        fputs("[INFO] Too many keys at the same time! Ignoring latest key...\n", stderr);
        return;
    }

    // Insert using already existing unique ordered array insert function
    size_t newSize = intPtrOrderedUniqueInsert(comboBuffer, *comboBufferN, keycode);
    // If size remained the same, value was already in the array, so ignore it
    // Note that this probably will never happen unless you have a keyboard with keys with equal keycodes (is there even a keyboard that does this? :S )
    if(newSize == *comboBufferN)
        fputs("[INFO] Ignoring key (already in combo buffer)...\n", stderr);
    else
        *comboBufferN = newSize;
}

// Removes a key from the comparison buffer
// Everything is pushed back to line up and the new size is updated. If a key couldn't be removed (not in buffer) do nothing, as it might have been ignored by insertKey
void removeKey(int* comboBuffer, size_t* comboBufferN, int keycode) {
    // Remove using already existing function
    *comboBufferN = intPtrRemove(comboBuffer, *comboBufferN, keycode);
}

// Executes a shell command in a non-blocking way
void doShellExec(char** command) {
    // Fork
    pid_t pid = fork();
    if(pid == 0) {
        // In the child process: Execute shell program
        if(execvp(command[0], command) == -1)
            fprintf(stderr, "[ERROR] Could not exec command: %s\n", strerror(errno));
        // This won't normally be executed, only if an error occurred
        // If an error indeed occurred, do the regular cleanup
        shutdownDaemon();
        exit(EXIT_FAILURE);
    }
    else if(pid == -1){
        // In the parent process, but child could not be created! :(
        // Print error message and DO NOT abort, just ignore
        fprintf(stderr, "[ERROR] Could not create child process, ignoring: %s\n", strerror(errno));
        fflush(stderr);
    }
    // Note that nothing happens if in the parent process and the child was successfully created, it just returns
}

// Prints parsed commands in a human-readable way. Args is a null terminated ARRAY (last element is a NULL pointer)
void printCommand(char** args, size_t size) {
    // If empty-ish, abort
    if(size <= 1)
        return;

    // Print argument by argument, excluding null pointer
    for(size_t n = 0; n < (size - 1); ++n) {
        if(n > 0)
            putchar(' ');
        putchar('"');
        fputs(args[n], stdout);
        putchar('"');
    }
}

// Like doBind but for a single key
void doSingleBind(int keycode) {
    // Iterate over all keybinds
    for(size_t i = 0; i < bindNum; ++i) {
        // Single sized and same keycode?
        if(comboBinds[i].size == 1 && comboBinds[i].codes[0] == keycode) {
            // Trigger keybind!
            fputs("Single bind triggered: ", stdout);
            printCommand(comboExecs[i].elems, comboExecs[i].size);
            putchar('\n');
            fflush(stdout);
            doShellExec(comboExecs[i].elems);
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
        if(comboBinds[i].size == comboBufferN) {
            // Same keycodes?
            size_t n = 0;
            for(; n < comboBufferN; ++n) {
                if(comboBinds[i].codes[n] != comboBuffer[n])
                    break; // Different keycode! Stop looping...
            }
            // If the loop didnt reach the end, then the keycodes didnt match. Skip this combo
            if(n != comboBufferN)
                continue;

            // Yes! Trigger keybind!
            fputs("Multi-key bind triggered: ", stdout);
            printCommand(comboExecs[i].elems, comboExecs[i].size);
            putchar('\n');
            fflush(stdout);
            doShellExec(comboExecs[i].elems);
            // All done! Return...
            return;
        }
    }
}

// Adds a keybind to memory
// Note that exec is NOT null terminated! That is why execSize is needed
// The keybind structs:
//   keyCombo { codes, size }* comboBinds
//   keyExec { data, elems, size }* comboExecs
//   bindNum
int addKeybind(int* keycodes, size_t keycodesSize, char* exec, size_t execSize) {
    //// Basic variable set-up
    // Increment bind counter
    ++bindNum;
    // Declare thisNum for convenience
    const size_t thisNum = bindNum - 1;

    //// Allocate main arrays
    // Allocate space for both structs
    if(bindNum == 0) { // First time allocating
        comboBinds = malloc(sizeof(struct keyCombo));
        comboExecs = malloc(sizeof(struct keyExec));
    }
    else { // Not first time allocating
        comboBinds = realloc(comboBinds, sizeof(struct keyCombo) * bindNum);
        comboExecs = realloc(comboExecs, sizeof(struct keyExec) * bindNum);
    }

    // Out of memory?
    if(comboBinds == NULL || comboExecs == NULL) {
        printMemoryErr();
        return 0;
    }

    //// Set default values to main arrays' last elements in case of out of memory error
    comboBinds[thisNum] = defaultKeyCombo;
    comboExecs[thisNum] = defaultKeyExec;

    //// Set actual values to comboBinds
    // Allocate space for the codes array
    comboBinds[thisNum].codes = malloc(sizeof(int) * keycodesSize);

    // Out of memory?
    if(comboBinds[thisNum].codes == NULL) {
        printMemoryErr();
        return 0;
    }

    // Update size
    comboBinds[thisNum].size = keycodesSize;

    // Update keycodes
    for(size_t n = 0; n < keycodesSize; ++n)
        comboBinds[thisNum].codes[n] = keycodes[n];

    //// Set actual values to comboExecs
    // Allocate space for the data array
    comboExecs[thisNum].data = malloc(execSize + 1);

    // Out of memory?
    if(comboExecs[thisNum].data == NULL) {
        printMemoryErr();
        return 0;
    }

    // Copy data using memcpy and append null terminator
    memcpy(comboExecs[thisNum].data, exec, execSize);
    comboExecs[thisNum].data[execSize] = '\0';

    // Increment size and allocate space for the first member
    comboExecs[thisNum].size = 1;
    comboExecs[thisNum].elems = malloc(sizeof(char*));
    
    // Out of memory?
    if(comboExecs[thisNum].elems == NULL) {
        printMemoryErr();
        return 0;
    }

    // Set first element pointer to first data position
    comboExecs[thisNum].elems[0] = comboExecs[thisNum].data;

    // Find other elements in raw data
    // Add next as element flag:
    // 0: Don't
    // 1: Do, normally
    // 2: Null terminate the ARRAY, NOT STRING
    char addNext = 0;
    for(char* n = comboExecs[thisNum].data; n <= (comboExecs[thisNum].data + execSize); ++n) {
        // Add element if flag is true or at the end of the array (for null terminator)
        if(n == (comboExecs[thisNum].data + execSize))
            addNext = 2;

        if(addNext != 0) {
            // Increment size
            ++comboExecs[thisNum].size;

            // Expand element array
            comboExecs[thisNum].elems = realloc(comboExecs[thisNum].elems, sizeof(char*) * comboExecs[thisNum].size);

            // Out of memory?
            if(comboExecs[thisNum].elems == NULL) {
                printMemoryErr();
                return 0;
            }

            if(addNext == 2) {
                // Null terminate array
                comboExecs[thisNum].elems[comboExecs[thisNum].size - 1] = NULL;
                break;
            }
            else {
                // Add new element normally
                comboExecs[thisNum].elems[comboExecs[thisNum].size - 1] = n;
                addNext = 0;
            }
        }

        // Nulls indicate the end of an element (and the beginning of another, therefore)
        if(*n == '\0')
            addNext = 1;
    }

    // All (finally) done!
    return 1;
}

// Loads ~/.babybindsrc, which contains all keybinds
// # indicate comments (like in shell scripts)
// All spaces, tabs, comments and empty lines are ignored
// ... unless in the shell command string, where spaces and tabs separate arguments
// Format is:
//   <keycode (int)>;<keycode>;<...>:<shell command (string)>
// Notes: 
// - the last separator is a colon, not a semicolon
// - only the first colon indicates the end of keycodes, all other syntax followed counts as the shell code
// - there are character escape sequences (escape character is the backslash [\]):
//   - \n for newlines
//   - \\ for backslashes
//   - \ <-(a space) for spaces
//   - \    <-(a tab) for tabs
//   - it is not possible to escape null characters (strings are terminated by null characters)
//     - programs typically handle this by using their own escape sequences anyway, so no worries (until someone complains, which is probably never)
//   - invalid escape sequences count as a backspace plus the next character (like if it was not an escape sequence in the first place)
//   - note that wildcard expansion is not supported and other special shell characters like quotes and asterisks are counted as regular characters (escape spaces instead!)
// - repeated spaces and tabs which are not escaped are ignored
// - there may be as many keycodes as possible, but they will be ignored if more than maxComboSize, discarding the whole combo
void loadConfig(const size_t maxComboSize) {
    // Get configuration file path
    // First, get the home path
    char* homePath = getenv("HOME");
    if(homePath == NULL) {
        fputs("[ERROR] Could not get home path! Aborting...\n", stderr);
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
    // 3: Escape next character (shell script mode)
    // 4: Comment (ignores everything until a newline)
    // 5: Error
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
        if((mode < 2 || mode > 3) && (c == ' ' || c == '\t'))
            continue;

        // If in starting mode, check for comment
        if(mode == 0) {
            if(c == '#')
                mode = 4;
        }

        // Check for newlines and EOF to save the parsed data
        if(c == '\n' || c == EOF) {
            if(mode == 1) {
                fputs("[ERROR] Malformed configuration file: Incomplete keybind (missing shell action)\n", stderr);
                mode = 5;
                break;
            }
            else if(mode == 2) {
                // Push data
                if(!addKeybind(parsedCombos, parsedCombosI, databuf, databufI)) {
                    mode = 5;
                    break;
                }

                // "Clear" the buffers
                databufI = 0;
                parsedCombosI = 0;
            }

            // Return to starting mode
            mode = 0;
        }
        else if(mode != 4) {
            // Add stuff to buffer if not in comment mode
            // ... but first, enter keycode mode if in starting mode
            if(mode == 0)
                mode = 1;

            // Parse data in buffer if switching mode
            if((c == ';' || c == ':') && (mode < 2 || mode > 3)) {
                // Field is empty (it can't be)
                if(databufI == 0) {
                    fputs("[ERROR] Malformed configuration file: Empty field\n", stderr);
                    mode = 5;
                    break;
                }

                // Keycode too big (bigger than 8 chars)
                if(databufI >= 8) {
                    fprintf(stderr, "[ERROR] Keycode is ridiculously big (%zu characters long!)\n", databufI);
                    mode = 5;
                    break;
                }

                // Too many keycodes in combo (equal to maxComboSize)
                if(parsedCombosI == maxComboSize) {
                    fprintf(stderr, "[ERROR] Key combo has too many keycodes (%zu)\n", maxComboSize);
                    mode = 5;
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
                        mode = 5;
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
                        mode = 5;
                        break;
                    }
                }

                if(mode == 1) { // Keycode mode: just insert
                    // Append data to buffer
                    databuf[databufI++] = c;
                }
                else if(mode == 2) { // Script mode: check for backslashes, spaces and tabs insert accordingly
                    if(c == '\\') {
                        databuf[databufI++] = '\\'; // Insert backslash anyway. Explanation:
                        // This is done because it is guaranteed that an escape sequence inserts at least one character. The escaped character will replace this one.
                        // It also avoids a repetition of the above buffer expansion in case of inserting 2 characters (happens on an invalid escape) avoiding problems
                        // Furthermore, in case of having an EOF after the backslash, it is guaranteed that the backslash is inserted (counts as an invalid escape)
                        
                        mode = 3; // Go to escape sequence mode
                    }
                    else if(c == ' ' || c == '\t') {
                        // Insert null if there was not a previous separator
                        if(databufI == 0 || databuf[databufI - 1] != '\0')
                            databuf[databufI++] = '\0'; // Insert null to represent new argument
                    }
                    else
                        databuf[databufI++] = c; // Just insert
                }
                else if(mode == 3 && c != '\\') { // Escaped script mode: parse escape sequences
                    // Note that backslashes are skipped because they are pre-inserted as a placeholder
                    if(c == ' ' || c == '\t')
                        databuf[databufI - 1] = c; // Escape the space/tab by replacing the previous placeholder backslash
                    else if(c == 'n')
                        databuf[databufI - 1] = '\n'; // Escape newline by replacing previous placeholder backslash
                    else
                        databuf[databufI++] = c; // Invalid escape sequence! Treat as a non escape sequence by inserting new char normally
                }
            }
        }
    } while(c != EOF); // Note: although we want to stop on EOF, we still want to update the shell script of the last bind, so we want to parse EOFs too

    // Clean-up this mess
    fclose(configFP);
    free(configPath);
    free(databuf);
    if(mode == 5) {
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

    //// Handle signals
    // On interrupt, use the interruptHandler function
    signal(SIGINT, interruptHandler);

    // Tell the kernel to automatically reap child processes (prevents defunct processes)
    signal(SIGCHLD, SIG_IGN);

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
        fputs("[ERROR] No input devices passed!\n", stderr);
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
    // Read fail counter
    unsigned char failNum = 0;

    //// Wait for keys and parse them
    puts("[INFO] Started! Interrupt to exit.");
    fflush(stdout);

    while(1) {
        n = read(devFD, &ev, sizeof(ev));
        if(n == sizeof(ev)) {
            // Read was successful! Parse stuff, reset fail counter and check if it is a keybind
            failNum = 0;
            if(ev.type == EV_KEY) { // Input is a key! Continue...
                // Notes:
                // - key autorepeats are ignored as we don't need to care about them for key combinations
                // - single-key keybinds are triggered on key release and ONLY IF ALONE
                // - multi-key keybinds are triggered on key press
                // - key combos are ordered by ev.code value

                if(ev.value == 0) { // Key released
                    removeKey(comboBuffer, &comboBufferN, ev.code);
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
            // Read errored! Skip this iteration, or abort, if too many failed reads.
            if(failNum == 10) {
                fputs("[ERROR] Input device read failed! Aborting (10 fails)...\n", stderr);
                break;
            }
            
            fputs("[ERROR] Input device read failed! Ignoring and waiting...\n", stderr);
            fflush(stderr);
            
            // Wait 3 seconds
            sleep(3);
            
            // Increment fail counter
            ++failNum;
        }
    }

    //// Clean-up
    shutdownDaemon();

    return EXIT_SUCCESS;
}
