#pragma once
/**
 * @file chunk.h
 * @brief Structures and enums for bytecode chunks.
 */
#include "kuroko.h"
#include "value.h"

/**
 * @brief Instruction opcode values
 *
 * The instruction opcode table is divided in four parts. The high two bits of each
 * opcode encodes the number of operands to pull from the codeobject and thus the
 * size (generally) of the instruction (note that OP_CLOSURE(_LONG) has additional
 * arguments depending on the function it points to).
 *
 * 0-operand opcodes are "simple" instructions that generally only deal with stack
 * values and require no additional arguments.
 *
 * 1- and 3- operand opcodes are paired as 'short' and 'long'. While the VM does not
 * currently depend on these instructions having the same values in the lower 6 bits,
 * it is recommended that this property remain true.
 *
 * 2-operand opcodes are generally jump instructions.
 */
typedef enum {
	OP_ADD = 1,
	OP_BITAND,
	OP_BITNEGATE,
	OP_BITOR,
	OP_BITXOR,
	OP_CLEANUP_WITH,
	OP_CLOSE_UPVALUE,
	OP_DIVIDE,
	OP_DOCSTRING,
	OP_EQUAL,
	OP_FALSE,
	OP_FINALIZE,
	OP_GREATER,
	OP_INHERIT,
	OP_INVOKE_DELETE,
	OP_INVOKE_DELSLICE,
	OP_INVOKE_GETSLICE,
	OP_INVOKE_GETTER,
	OP_INVOKE_SETSLICE,
	OP_INVOKE_SETTER,
	OP_IS,
	OP_LESS,
	OP_MODULO,
	OP_MULTIPLY,
	OP_NEGATE,
	OP_NONE,
	OP_NOT,
	OP_POP,
	OP_POW,
	OP_RAISE,
	OP_RETURN,
	OP_SHIFTLEFT,
	OP_SHIFTRIGHT,
	OP_SUBTRACT,
	OP_SWAP,
	OP_TRUE,
	OP_FILTER_EXCEPT,
	OP_INVOKE_ITER,
	OP_INVOKE_CONTAINS,
	OP_BREAKPOINT, /* NEVER output this instruction in the compiler or bad things can happen */
	OP_YIELD,
	OP_ANNOTATE,
	/* current highest: 44 */

	OP_CALL = 64,
	OP_CLASS,
	OP_CLOSURE,
	OP_CONSTANT,
	OP_DEFINE_GLOBAL,
	OP_DEL_GLOBAL,
	OP_DEL_PROPERTY,
	OP_DUP,
	OP_EXPAND_ARGS,
	OP_GET_GLOBAL,
	OP_GET_LOCAL,
	OP_GET_PROPERTY,
	OP_GET_SUPER,
	OP_GET_UPVALUE,
	OP_IMPORT,
	OP_IMPORT_FROM,
	OP_INC,
	OP_KWARGS,
	OP_CLASS_PROPERTY,
	OP_SET_GLOBAL,
	OP_SET_LOCAL,
	OP_SET_PROPERTY,
	OP_SET_UPVALUE,
	OP_TUPLE,
	OP_UNPACK,
	OP_LIST_APPEND,
	OP_DICT_SET,
	OP_SET_ADD,
	OP_MAKE_LIST,
	OP_MAKE_DICT,
	OP_MAKE_SET,
	OP_REVERSE,

	OP_JUMP_IF_FALSE = 128,
	OP_JUMP_IF_TRUE,
	OP_JUMP,
	OP_LOOP,
	OP_PUSH_TRY,
	OP_PUSH_WITH,

	OP_CALL_LONG = 192,
	OP_CLASS_LONG,
	OP_CLOSURE_LONG,
	OP_CONSTANT_LONG,
	OP_DEFINE_GLOBAL_LONG,
	OP_DEL_GLOBAL_LONG,
	OP_DEL_PROPERTY_LONG,
	OP_DUP_LONG,
	OP_EXPAND_ARGS_LONG,
	OP_GET_GLOBAL_LONG,
	OP_GET_LOCAL_LONG,
	OP_GET_PROPERTY_LONG,
	OP_GET_SUPER_LONG,
	OP_GET_UPVALUE_LONG,
	OP_IMPORT_LONG,
	OP_IMPORT_FROM_LONG,
	OP_INC_LONG,
	OP_KWARGS_LONG,
	OP_CLASS_PROPERTY_LONG,
	OP_SET_GLOBAL_LONG,
	OP_SET_LOCAL_LONG,
	OP_SET_PROPERTY_LONG,
	OP_SET_UPVALUE_LONG,
	OP_TUPLE_LONG,
	OP_UNPACK_LONG,
	OP_LIST_APPEND_LONG,
	OP_DICT_SET_LONG,
	OP_SET_ADD_LONG,
	OP_MAKE_LIST_LONG,
	OP_MAKE_DICT_LONG,
	OP_MAKE_SET_LONG,
	OP_REVERSE_LONG,
} KrkOpCode;

/**
 * @brief Map entry of instruction offsets to line numbers.
 *
 * Each code object contains an array of line mappings, indicating
 * the start offset of each line. Since a line typically maps to
 * multiple opcodes, and spans of many lines may map to no opcodes
 * in the case of blank lines or docstrings, this array is stored
 * as a sequence of <starOffset, line> pairs rather than a simple
 * array of one or the other.
 */
typedef struct {
	size_t startOffset;
	size_t line;
} KrkLineMap;

/**
 * @brief Opcode chunk of a code object.
 *
 * Opcode chunks are internal to code objects and I'm not really
 * sure why we're still separating them from the KrkCodeObjects.
 *
 * Stores four flexible arrays using three different formats:
 * - Code, representing opcodes and operands.
 * - Lines, representing offset-to-line mappings.
 * - Filename, the string name of the source file.
 * - Constants, an array of values referenced by the code object.
 */
typedef struct {
	size_t  count;
	size_t  capacity;
	uint8_t * code;

	size_t linesCount;
	size_t linesCapacity;
	KrkLineMap * lines;

	KrkString * filename;
	KrkValueArray constants;
} KrkChunk;

/**
 * @brief Initialize an opcode chunk.
 * @memberof KrkChunk
 */
extern void krk_initChunk(KrkChunk * chunk);

/**
 * @memberof KrkChunk
 * @brief Append a byte to an opcode chunk.
 */
extern void krk_writeChunk(KrkChunk * chunk, uint8_t byte, size_t line);

/**
 * @brief Release the resources allocated to an opcode chunk.
 * @memberof KrkChunk
 */
extern void krk_freeChunk(KrkChunk * chunk);

/**
 * @brief Add a new constant value to an opcode chunk.
 * @memberof KrkChunk
 */
extern size_t krk_addConstant(KrkChunk * chunk, KrkValue value);

/**
 * @brief Write an OP_CONSTANT(_LONG) instruction.
 * @memberof KrkChunk
 */
extern void krk_emitConstant(KrkChunk * chunk, size_t ind, size_t line);

/**
 * @brief Add a new constant and write an instruction for it.
 * @memberof KrkChunk
 */
extern size_t krk_writeConstant(KrkChunk * chunk, KrkValue value, size_t line);
