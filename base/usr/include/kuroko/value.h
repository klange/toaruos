#pragma once
/**
 * @file value.h
 * @brief Definitions for primitive stack references.
 */
#include <stdio.h>
#include "kuroko.h"

/**
 * @brief Base structure of all heap objects.
 *
 * KrkObj is the base type of all objects stored on the heap and
 * managed by the garbage collector.
 */
typedef struct KrkObj KrkObj;
typedef struct KrkString KrkString;

/**
 * @brief Tag enum for basic value types.
 *
 * Value types are tagged unions of a handful of small
 * types represented directly on the stack: Integers,
 * double-precision floating point values, booleans,
 * exception handler references, complex function argument
 * processing sentinels, object reference pointers, and None.
 */
typedef enum {
	KRK_VAL_NONE,
	KRK_VAL_BOOLEAN,
	KRK_VAL_INTEGER,
	KRK_VAL_FLOATING,
	KRK_VAL_HANDLER,
	KRK_VAL_OBJECT,
	KRK_VAL_KWARGS,
} KrkValueType;

/**
 * @brief Stack value representation of a 'with' or 'try' block.
 *
 * When a 'with' or 'try' block is entered, a handler value is
 * created on the stack representing the type (with, try) and the
 * jump target to leave the block (entering the 'except' block of
 * a 'try', if present, or calling the __exit__ method of an object
 * __enter__'d by a 'with' block). When the relevant conditions are
 * triggered in the VM, the stack will be scanned from top to bottom
 * to look for these values.
 */
typedef struct {
	unsigned short type;
	unsigned short target;
} KrkJumpTarget;

/**
 * @brief Stack reference or primative value.
 *
 * This type stores a stack reference to an object, or the contents of
 * a primitive type. Each VM thread's stack consists of an array of
 * these values, and they are generally passed around in the VM through
 * direct copying rather than as pointers, avoiding the need to track
 * memory used by them.
 *
 * Each value is a tagged union with a type (see the enum KrkValueType)
 * and its contents.
 */
typedef struct {
	KrkValueType type;
	union {
		char boolean;
		krk_integer_type integer;
		double  floating;
		KrkJumpTarget handler;
		KrkObj *   object;
	} as;
} KrkValue;

/**
 * @brief Flexible vector of stack references.
 *
 * Value Arrays provide a resizable collection of values and are the
 * backbone of lists and tuples.
 */
typedef struct {
	size_t capacity;   /**< Available allocated space. */
	size_t count;      /**< Current number of used slots. */
	KrkValue * values; /**< Pointer to heap-allocated storage. */
} KrkValueArray;

/**
 * @brief Initialize a value array.
 * @memberof KrkValueArray
 *
 * This should be called for any new value array, especially ones
 * initialized in heap or stack space, to set up the capacity, count
 * and initial value pointer.
 *
 * @param array Value array to initialize.
 */
extern void krk_initValueArray(KrkValueArray * array);

/**
 * @brief Add a value to a value array.
 * @memberof KrkValueArray
 *
 * Appends 'value' to the end of the given array, adjusting count values
 * and resizing as necessary.
 *
 * @param array Array to append to.
 * @param value Value to append to array.
 */
extern void krk_writeValueArray(KrkValueArray * array, KrkValue value);

/**
 * @brief Release relesources used by a value array.
 * @memberof KrkValueArray
 *
 * Frees the storage associated with a given value array and resets
 * its capacity and count. Does not directly free resources associated
 * with heap objects referenced by the values in this array: The GC
 * is responsible for taking care of that.
 *
 * @param array Array to release.
 */
extern void krk_freeValueArray(KrkValueArray * array);

/**
 * @brief Print a string representation of a value.
 * @memberof KrkValue
 *
 * Print a string representation of 'value' to the stream 'f'.
 * For primitives, performs appropriate formatting. For objects,
 * this will call __str__ on the object's representative type.
 * If the type does not have a __str__ method, __repr__ will be
 * tried before falling back to krk_typeName to directly print
 * the name of the class with no information on the value.
 *
 * This function provides the backend for the print() built-in.
 *
 * @param f     Stream to write to.
 * @param value Value to display.
 */
extern void krk_printValue(FILE * f, KrkValue value);

/**
 * @brief Print a value without calling the VM.
 * @memberof KrkValue
 *
 * Print a string representation of 'value' to the stream 'f',
 * avoiding calls to managed code by using simplified representations
 * where necessary. This is intended for use in debugging code, such
 * as during disassembly, or when printing values in an untrusted context.
 *
 * @note This function will truncate long strings and print them in a form
 *       closer to the 'repr()' representation, with escaped bytes, rather
 *       than directly printing them to the stream.
 *
 * @param f     Stream to write to.
 * @param value Value to display.
 */
extern void krk_printValueSafe(FILE * f, KrkValue value);

/**
 * @brief Compare two values for equality.
 * @memberof KrkValue
 *
 * Performs a relaxed equality comparison between two values,
 * check for equivalence by contents. This may call managed
 * code to run __eq__ methods.
 *
 * @return 1 if values are equivalent, 0 otherwise.
 */
extern int krk_valuesEqual(KrkValue a, KrkValue b);

/**
 * @brief Compare two values by identity.
 * @memberof KrkValue
 *
 * Performs a strict comparison between two values, comparing
 * their identities. For primitive values, this is generally
 * the same as comparing by equality. For objects, this compares
 * pointer values directly.
 *
 * @return 1 if values represent the same object or value, 0 otherwise.
 */
extern int krk_valuesSame(KrkValue a, KrkValue b);

#define BOOLEAN_VAL(value)  ((KrkValue){KRK_VAL_BOOLEAN, {.integer = value}})
#define NONE_VAL(value)     ((KrkValue){KRK_VAL_NONE,    {.integer = 0}})
#define INTEGER_VAL(value)  ((KrkValue){KRK_VAL_INTEGER, {.integer = value}})
#define FLOATING_VAL(value) ((KrkValue){KRK_VAL_FLOATING,{.floating = value}})
#define HANDLER_VAL(ty,ta)  ((KrkValue){KRK_VAL_HANDLER, {.handler = (KrkJumpTarget){.type = ty, .target = ta}}})
#define OBJECT_VAL(value)   ((KrkValue){KRK_VAL_OBJECT,  {.object = (KrkObj*)value}})
#define KWARGS_VAL(value)   ((KrkValue){KRK_VAL_KWARGS,  {.integer = value}})

#define AS_BOOLEAN(value)   ((value).as.integer)
#define AS_INTEGER(value)   ((value).as.integer)
#define AS_FLOATING(value)  ((value).as.floating)
#define AS_HANDLER(value)   ((value).as.handler)
#define AS_OBJECT(value)    ((value).as.object)

#define IS_BOOLEAN(value)   ((value).type == KRK_VAL_BOOLEAN)
#define IS_NONE(value)      ((value).type == KRK_VAL_NONE)
#define IS_INTEGER(value)   (((value).type == KRK_VAL_INTEGER) || ((value.type) == KRK_VAL_BOOLEAN))
#define IS_FLOATING(value)  ((value).type == KRK_VAL_FLOATING)
#define IS_HANDLER(value)   ((value).type == KRK_VAL_HANDLER)
#define IS_OBJECT(value)    ((value).type == KRK_VAL_OBJECT)
#define IS_KWARGS(value)    ((value).type == KRK_VAL_KWARGS)

#define IS_TRY_HANDLER(value)  (IS_HANDLER(value) && AS_HANDLER(value).type == OP_PUSH_TRY)
#define IS_WITH_HANDLER(value) (IS_HANDLER(value) && AS_HANDLER(value).type == OP_PUSH_WITH)

