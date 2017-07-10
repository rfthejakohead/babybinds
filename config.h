#ifndef BABYBINDS_CONFIG_H
#define BABYBINDS_CONFIG_H

/* For datatypes */
#include "datatypes.h"

/* For adding to keybind globals */
#include "globals.h"

/* For clean-up */
#include "call.h"

/* Adds a keybind to memory
   Note that exec is NOT null terminated! That is why execSize is needed
   The keybind structs:
     keyCombo { codes, size }* comboBinds
     keyExec { data, elems, size }* comboExecs
     bindNum */
int addKeybind(int* keycodes, size_t keycodesSize, char* exec, size_t execSize);

/* Loads ~/.babybindsrc, which contains all keybinds
   # indicate comments (like in shell scripts)
   All spaces, tabs, comments and empty lines are ignored
   ... unless in the shell command string, where spaces and tabs separate arguments
   Format is:
     <keycode (int)>;<keycode>;<...>:<shell command (string)>
   Notes: 
   - the last separator is a colon, not a semicolon
   - only the first colon indicates the end of keycodes, all other syntax followed counts as the shell code
   - there are character escape sequences (escape character is the backslash [\]):
     - \n for newlines
     - \\ for backslashes
     - \ <-(a space) for spaces
     - \    <-(a tab [there isn't really a tab there if you're still wondering, just 4 spaces]) for tabs
     - it is not possible to escape null characters (strings are terminated by null characters)
       - programs typically handle this by using their own escape sequences anyway, so no worries (until someone complains, which is probably never)
     - invalid escape sequences count as a backspace plus the next character (like if it was not an escape sequence in the first place)
     - note that wildcard expansion is not supported and other special shell characters like quotes and asterisks are counted as regular characters (escape spaces instead!)
   - repeated spaces and tabs which are not escaped are ignored
   - there may be as many keycodes as possible, but they will be ignored if more than BABYBINDS_COMBOBUFFER_SIZE, discarding the whole combo */
void loadConfig(void);

#endif
