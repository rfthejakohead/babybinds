#ifndef BABYBINDS_MEMORY_H
#define BABYBINDS_MEMORY_H
/***** For all stuff related to memory management (excluding non-memory-manager datatypes) and array manipulation used by babybinds *****/
/* Standard includes */
#include <stdio.h>
#include <stdlib.h>

/* Returns salloc_fv from memory.c */
int salloc_f(void);

/* A wrapper for *alloc():
 * - Takes the same 2 arguments of realloc
 * - Returns the resulting pointer to data and only updates it on success, else it remains unchanged
 *   - This is done so that memory can still be accessed when allocation fails
 * - Returns the resulting pointer like *alloc
 * - Prints a custom memory error message on failure with the argument values and their pointing values
 * - Sets the global salloc_f flag to 1 on failure (not thread safe, but it's probably never going to be multi-threaded anyway)
 */
void* salloc(void* ptr, size_t size);

/* A wrapper for free():
 * - Returns NULL. ALWAYS.
 *   - This is handy, because you can do for example: a = sfree(a)
 *   - This prevents memory errors due to trying to free stuff that isn't allocated
 *     - It warns you when you try to free a NULL pointer
 * - Behaves like free
 */
void* sfree(void* ptr);

/* Inserts an unique value (can only be one) to a fixed size int array using insertion sort
   Returns final size. If the size remains the same, an error occured
   Note that this function does not handle the check to see if the array will exceed its maximum size */
size_t intPtrOrderedUniqueInsert(int* array, size_t size, int val);

/* Removes an element from a fixed size int array by value
   Returns final size. If the size remains the same, an error occured */
size_t intPtrRemove(int* array, size_t size, int val);

#endif
