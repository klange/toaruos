#pragma once
/**
 * @file debug.h
 * @brief Functions for debugging bytecode execution.
 *
 * This header provides functions for disassembly bytecode to
 * readable instruction traces, mapping bytecode offsets to
 * source code lines, and handling breakpoint instructions.
 *
 * Several of these functions are also exported to user code
 * in the @ref mod_dis module.
 *
 * Note that these functions are not related to manage code
 * exception handling, but instead inteaded to provide a low
 * level interface to the VM's execution process and allow
 * for the implementation of debuggers and debugger extensions.
 */
#include <stdio.h>
#include "vm.h"
#include "chunk.h"
#include "object.h"

/**
 * @brief Print a disassembly of 'func' to the stream 'f'.
 *
 * Generates and prints a bytecode disassembly of the code object 'func',
 * writing it to the requested stream.
 *
 * @param f     Stream to write to.
 * @param func  Code object to disassemble.
 * @param name  Function name to display in disassembly output.
 */
extern void krk_disassembleCodeObject(FILE * f, KrkCodeObject * func, const char * name);

/**
 * @brief Print a disassembly of a single opcode instruction.
 *
 * Generates and prints a bytecode disassembly for one instruction from
 * the code object 'func' at byte offset 'offset', printing the result to
 * the requested stream and returning the size of the instruction.
 *
 * @param f      Stream to write to.
 * @param func   Code object to disassemble.
 * @param offset Byte offset of the instruction to disassemble.
 * @return The size of the instruction in bytes.
 */
extern size_t krk_disassembleInstruction(FILE * f, KrkCodeObject * func, size_t offset);

/**
 * @brief Obtain the line number for a byte offset into a bytecode chunk.
 *
 * Scans the line mapping table for the given chunk to find the
 * correct line number from the original source file for the instruction
 * at byte index 'offset'.
 *
 * @param chunk  Bytecode chunk containing the instruction.
 * @param offset Byte offset of the instruction to locate.
 * @return Line number, 1-indexed.
 */
extern size_t krk_lineNumber(KrkChunk * chunk, size_t offset);

/* Internal stuff */
extern void _createAndBind_disMod(void);

/**
 * @brief Called by the VM when a breakpoint is encountered.
 *
 * Internal method, should not generally be called.
 */
extern int krk_debugBreakpointHandler(void);

/**
 * @brief Called by the VM on single step.
 *
 * Handles calling the registered debugger hook, if one has
 * been set, and performing the requested response action.
 * Also takes care of re-enabling REPEAT breakpoints.
 *
 * Internal method, should not generally be called.
 */
extern int krk_debuggerHook(KrkCallFrame * frame);

/**
 * @brief Function pointer for a debugger hook.
 * @ref krk_debug_registerCallback()
 */
typedef int (*KrkDebugCallback)(KrkCallFrame *frame);

/**
 * @brief Register a debugger callback.
 *
 * The registered function @p hook will be called when an
 * OP_BREAKPOINT instruction is encountered. The debugger
 * is provided a pointer to the active frame and can use
 * functions from the krk_debug_* suite to examine the
 * thread state, execute more code, and resume execution.
 *
 * The debugger hook will be called from the thread that
 * encountered the breakpoint, and should return one of
 * the KRK_DEBUGGER_ status values to inform the VM of
 * what action to take.
 *
 * @param hook The hook function to attach.
 * @return 0 if the hook was registered; 1 if a hook was
 *         already registered, in which case the new hook
 *         has not been registered.
 */
extern int krk_debug_registerCallback(KrkDebugCallback hook);

/**
 * @brief Add a breakpoint to the given line of a file.
 *
 * The interpreter will scan all code objects and attach
 * a breakpoint instruction to the first such object that
 * has a match filename and contains an instruction with
 * a matching line mapping. Breakpoints can only be added
 * to code which has already been compiled.
 *
 * @param filename KrkString * representation of a source
 *                 code filename to look for.
 * @param line The line to set the breakpoint at.
 * @param flags Allows configuring the disposition of the breakpoint.
 * @return A breakpoint identifier handle on success, or -1 on failure.
 */
extern int krk_debug_addBreakpointFileLine(KrkString * filename, size_t line, int flags);

/**
 * @brief Add a breakpoint to the given code object.
 *
 * A new breakpoint is added to the breakpoint table and
 * the code object's bytecode is overwritten to insert
 * the OP_BREAKPOINT instruction.
 *
 * Callers must ensure that @p offset points to the opcode
 * portion of an instruction, and not an operand, or the
 * attempt to add a breakpoint can corrupt the bytecode.
 *
 * @param codeObject KrkCodeObject* for the code object to break on.
 * @param offset Bytecode offset to insert the breakpoint at.
 * @param flags Allows configuring the disposition of the breakpoint.
 * @return A breakpoint identifier handle on success, or -1 on failure.
 */
extern int krk_debug_addBreakpointCodeOffset(KrkCodeObject * codeObject, size_t offset, int flags);

/**
 * @brief Remove a breakpoint from the breakpoint table.
 *
 * Removes the breakpoint @p breakpointId from the breakpoint
 * table, restoring the bytecode instruction for it if it
 * was enabled.
 *
 * @param breakpointId The breakpoint to remove.
 * @return 0 on success, 1 if the breakpoint identifier is invalid.
 */
extern int krk_debug_removeBreakpoint(int breakpointId);

/**
 * @brief Enable a breakpoint.
 *
 * Writes the OP_BREAKPOINT instruction into the function
 * bytecode chunk for the given breakpoint.
 *
 * @param breakpointId The breakpoint to enable.
 * @return 0 on success, 1 if the breakpoint identifier is invalid.
 */
extern int krk_debug_enableBreakpoint(int breakpointId);

/**
 * @brief Disable a breakpoint.
 *
 * Restores the bytecode instructions for the given breakpoint
 * if it is currently enabled.
 *
 * No error is returned if the breakpoint was already disabled.
 *
 * @param breakpointId The breakpoint to disable.
 * @return 0 on success, 1 if the breakpoint identifier is invalid.
 */
extern int krk_debug_disableBreakpoint(int breakpointId);

/**
 * @brief Enable single stepping in the current thread.
 */
extern void krk_debug_enableSingleStep(void);

/**
 * @brief Disable single stepping in the current thread.
 */
extern void krk_debug_disableSingleStep(void);

/**
 * @brief Safely dump a traceback to stderr.
 *
 * Wraps @ref krk_dumpTraceback() so it can be safely
 * called from a debugger.
 */
extern void krk_debug_dumpTraceback(void);

/**
 * @brief Retreive information on a breakpoint.
 *
 * Can be called by debuggers to examine the details of a breakpoint by its handle.
 * Information is returned through the pointers provided as parameters.
 *
 * @param breakIndex Breakpoint handle to examine.
 * @param funcOut    (Out) The code object this breakpoint is in.
 * @param offsetOut  (Out) The bytecode offset within the code object where the breakpoint is located.
 * @param flagsOut   (Out) The configuration flags for the breakpoint.
 * @param enabledOut (Out) Whether the breakpoint is enabled or not.
 * @return 0 on success, -1 on out of range, -2 if the selected slot was removed.
 */
extern int krk_debug_examineBreakpoint(int breakIndex, KrkCodeObject ** funcOut, size_t * offsetOut, int * flagsOut, int *enabledOut);

/**
 * @brief Print the elements on the stack.
 *
 * Prints the elements on the stack for the current thread to @p file,
 * highlighting @p frame as the activate call point and indicating
 * where its arguments start.
 */
extern void krk_debug_dumpStack(FILE * f, KrkCallFrame * frame);

/**
 * @def KRK_BREAKPOINT_NORMAL
 *
 * This breakpoint should fire once and then remain in the table
 * to be re-enabled later.
 *
 * @def KRK_BREAKPOINT_ONCE
 *
 * This breakpoint should fire once and then be removed from the
 * breakpoint table.
 *
 * @def KRK_BREAKPOINT_REPEAT
 *
 * After this breakpoint has fired and execution resumes, the
 * interpreter should re-enable it to fire again until it is
 * removed by a call to @ref krk_debug_removeBreakpoint()
 */
#define KRK_BREAKPOINT_NORMAL  0
#define KRK_BREAKPOINT_ONCE    1
#define KRK_BREAKPOINT_REPEAT  2

#define KRK_DEBUGGER_CONTINUE  0
#define KRK_DEBUGGER_ABORT     1
#define KRK_DEBUGGER_STEP      2
#define KRK_DEBUGGER_RAISE     3
#define KRK_DEBUGGER_QUIT      4
