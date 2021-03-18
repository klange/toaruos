#pragma once
/**
 * @file memory.h
 * @brief Functions for dealing with garbage collection and memory allocation.
 */
#include "kuroko.h"
#include "object.h"
#include "table.h"

#define GROW_CAPACITY(c) ((c) < 8 ? 8 : (c) * 2)
#define GROW_ARRAY(t,p,o,n) (t*)krk_reallocate(p,sizeof(t)*o,sizeof(t)*n)

#define FREE_ARRAY(t,a,c) krk_reallocate(a,sizeof(t) * c, 0)
#define FREE(t,p) krk_reallocate(p,sizeof(t),0)

#define ALLOCATE(type, count) (type*)krk_reallocate(NULL,0,sizeof(type)*(count))

/**
 * @brief Resize an allocated heap object.
 *
 * Allocates or reallocates the heap object 'ptr', tracking changes
 * in sizes from 'old' to 'new'. If 'ptr' is NULL, 'old' should be 0,
 * and a new pointer will be allocated of size 'new'.
 *
 * @param ptr Heap object to resize.
 * @param old Current size of the object.
 * @param new New size of the object.
 * @return New pointer for heap object.
 */
extern void * krk_reallocate(void * ptr, size_t old, size_t new);

/**
 * @brief Release all objects.
 *
 * Generally called automatically by krk_freeVM(); releases all of
 * the GC-tracked heap objects.
 */
extern void krk_freeObjects(void);

/**
 * @brief Run a cycle of the garbage collector.
 *
 * Runs one scan-sweep cycle of the garbage collector, potentially
 * freeing unused resources and advancing potentially-unused
 * resources to the next stage of removal.
 *
 * @return The number of bytes released by this collection cycle.
 */
extern size_t krk_collectGarbage(void);

/**
 * @brief During a GC scan cycle, mark a value as used.
 *
 * When defining a new type in a C extension, this function should
 * be used by the type's _ongcscan callback to mark any values not
 * already tracked by the garbage collector.
 *
 * @param value The value to mark.
 */
extern void krk_markValue(KrkValue value);

/**
 * @brief During a GC scan cycle, mark an object as used.
 *
 * Equivalent to krk_markValue but operates directly on an object.
 *
 * @param object The object to mark.
 */
extern void krk_markObject(KrkObj * object);

/**
 * @brief During a GC scan cycle, mark the contents of a table as used.
 *
 * Marks all keys and values in a table as used. Generally applied
 * to the internal storage of mapping types.
 *
 * @param table The table to mark.
 */
extern void krk_markTable(KrkTable * table);

