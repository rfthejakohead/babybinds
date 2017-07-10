/***** printmsgs.h implementation *****/
#include "printmsgs.h"

void taggedMsg2(enum tagErrorLevel tags, const char* str1, char* str2) {
    /* Stream to output to */
    FILE* ostream;

    /* Print tag and update output stream */
    switch(tags & TM_level) {
    case TM_info:
        ostream = stdout;
        fputs("[INFO] ", ostream);
        break;
    case TM_warning:
        ostream = stderr;
        fputs("[WARNING] ", ostream);
        break;
    case TM_error:
        ostream = stderr;
        fputs("[ERROR] ", ostream);
        break;
    default:
        /* This should never happen! */
        ostream = stderr;
        fputs("[UNKNOWN (REPORT ME!)] ", ostream);
        break;
    }

    /* Print first string */
    fputs(str1, ostream);

    /* Print second string if passed */
    if(str2 != NULL)
        fputs(str2, ostream);

    /* Print newline if told to by tag */
    if((tags & TM_newline) == TM_newline)
        fputc('\n', ostream);

    /* Flush if told to by tag */
    if((tags & TM_flush) == TM_flush)
        fflush(ostream);
}

void taggedMsg(enum tagErrorLevel tags, const char* str) {
    taggedMsg2(tags, str, NULL);
}

void printUsage(const char* binName) {
    printf("Usage:\n");
    printf("%s <input device path>\n", binName);
    fflush(stdout);
}

void printCommand(char** args, size_t size) {
    size_t n;
    
    /* If empty-ish, abort */
    if(size <= 1)
        return;

    /* Print argument by argument, excluding null pointer */
    for(n = 0; n < (size - 1); ++n) {
        if(n > 0)
            putchar(' ');
        putchar('"');
        fputs(args[n], stdout);
        putchar('"');
    }
}

