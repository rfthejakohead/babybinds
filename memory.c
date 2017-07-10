/***** memory.h implementation *****/
#include "memory.h"

/* A global flag that indicates a memory error on a failed salloc. If 1, a failure has occured */
static int salloc_fv = 0;

int salloc_f(void) {
    return salloc_fv;
}

void* salloc(void* ptr, size_t size) {
    /* Temporary pointer to check if NULL before updating final pointer */
    void* tmpPtr;
    
    /* Allocate memory. Realloc is used here because it handles NULL pointers aswell to behave like malloc */
    tmpPtr = realloc(ptr, size);

    if(tmpPtr == NULL) { /* Oh noes! Something went wrong! */
        /* Print scary memory error message */
        fprintf(stderr, "[ERROR] No memory available:\n - realloc@salloc(void* ptr = %p, size_t size = %lu) returned NULL!\n", ptr, (unsigned long)size);
        fflush(stderr);

        /* Update global flag */
        salloc_fv = 1;

        /* Return previous working memory address */
        return ptr;
    }

    /* Return updated pointer */
    return tmpPtr;
}

void* sfree(void* ptr) {
    if(ptr == NULL) { /* Warning if pointer is already null */
        fputs("[WARNING] Attempting to free NULL. This is ok-ish, but memory might be trying to be freed multiple times.\n", stderr);
        fflush(stderr);
    }
    else /* free normally */
        free(ptr);

    /* Return NULL for convenience */
    return NULL;
}

size_t intPtrOrderedUniqueInsert(int* array, size_t size, int val) {
    size_t i;
    size_t si;

    /* Special case: if the array is empty (size of 0) just insert the value */
    if(size == 0) {
        array[0] = val;
        return 1;
    }

    /* Get insert position using a (kind of) insertion sort */
    for(i = 0; i < size; ++i) {
        if(val < array[i]) /* Value is smaller than this element, position it here */
            break;
        else if(val == array[i]) /* Value is equal to this one! Ignore this insert as value is already in array and it must be unique */
            return size;
    }
    
    /* Shift all elements after the position of the new element to the right for ordering */
    for(si = size - 1; si >= i; --si) {
        array[si + 1] = array[si];
        if(si == 0)
            break; /* Prevents segfault due to underflow */
    }

    /* Add value to resulting position */
    array[i] = val;

    /* Return new size */
    return size + 1;
}

size_t intPtrRemove(int* array, size_t size, int val) {
    size_t i;

    /* Get element associated with value */
    for(i = 0; i < size; ++i) {
        if(array[i] == val)
            break;
    }

    /* If the position is equal to size, abort, as it means the value was not found in the array */
    if(i == size)
        return size;

    /* Remove the element from the array by shifting all values left (last element's value not cleared!) */
    for(; i < size - 1; ++i)
        array[i] = array[i + 1];

    /* Return new size */
    return size - 1;
}

