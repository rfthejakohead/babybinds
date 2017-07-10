/***** call.h implementation *****/
#include "call.h"

void shutdownDaemon(void) {
    size_t n;
    
    /* Check if the input device was valid and close it if it was */
    if(devFD > -1)
        close(devFD);
    
    for(n = 0; n < bindNum; ++n) {
        if(comboBinds != NULL)
            sfree(comboBinds[n].codes);
        if(comboExecs != NULL) {
            sfree(comboExecs[n].elems);
            sfree(comboExecs[n].data);
        }
    }
    sfree(comboBinds);
    sfree(comboExecs);
}

void interruptHandler(int signum) {
    if(signum == SIGINT) {
        taggedMsg(TM_info | TM_flush | TM_newline, "Interrupt caught! Shutting down gracefully...");
        shutdownDaemon();
        exit(EXIT_SUCCESS);
    }
}

void doShellExec(char** command) {
    /* Fork */
    pid_t pid = fork();
    if(pid == 0) {
        /* In the child process: Execute shell program */
        if(execvp(command[0], command) == -1)
            taggedMsg2(TM_warning | TM_flush | TM_newline, "Could not exec command: ", strerror(errno));
        /* This won't normally be executed, only if an error occurred
           If an error indeed occurred, do the regular cleanup */
        shutdownDaemon();
        exit(EXIT_FAILURE);
    }
    else if(pid == -1){
        /* In the parent process, but child could not be created! :(
           Print error message and DO NOT abort, just ignore */
        taggedMsg2(TM_warning | TM_flush | TM_newline, "Could not create child process, ignoring: ", strerror(errno));
    }
    /* Note that nothing happens if in the parent process and the child was successfully created, it just returns */
}

void doSingleBind(int keycode) {
    /* Iterate over all keybinds */
    size_t i;
    for(i = 0; i < bindNum; ++i) {
        /* Single sized and same keycode? */
        if(comboBinds[i].size == 1 && comboBinds[i].codes[0] == keycode) {
            /* Trigger keybind! */
            fputs("Single bind triggered: ", stdout);
            printCommand(comboExecs[i].elems, comboExecs[i].size);
            putchar('\n');
            fflush(stdout);
            doShellExec(comboExecs[i].elems);
            /* All done! Return... */
            return;
        }
    }
}

void doBind(int* comboBuffer, size_t comboBufferN) {
    size_t i;
    
    /* Iterate over all keybinds */
    for(i = 0; i < bindNum; ++i) {
        /* Same size? */
        if(comboBinds[i].size == comboBufferN) {
            size_t n;
            
            /* Same keycodes? */
            for(n = 0; n < comboBufferN; ++n) {
                if(comboBinds[i].codes[n] != comboBuffer[n])
                    break; /* Different keycode! Stop looping... */
            }
            /* If the loop didnt reach the end, then the keycodes didnt match. Skip this combo */
            if(n != comboBufferN)
                continue;

            /* Yes! Trigger keybind! */
            fputs("Multi-key bind triggered: ", stdout);
            printCommand(comboExecs[i].elems, comboExecs[i].size);
            putchar('\n');
            fflush(stdout);
            doShellExec(comboExecs[i].elems);
            /* All done! Return... */
            return;
        }
    }
}

