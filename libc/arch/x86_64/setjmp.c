#include <setjmp.h>


__attribute__((naked))
__attribute__((returns_twice))
int setjmp(jmp_buf env) {
	asm volatile (
		"leaq 8(%%rsp), %%rax\n" /* Return address into rax */
		"movq %%rax, 0(%%rdi)\n" /* jmp_buf[0] = rsp */
		"movq %%rbp, 8(%%rdi)\n" /* jmp_buf[1] = rbp */
		"movq (%%rsp), %%rax\n"
		"movq %%rax, 16(%%rdi)\n" /* jmp_buf[2] = return address */
		"movq %%rbx, 24(%%rdi)\n" /* jmp_buf[3] = rbx */
		"movq %%r12, 32(%%rdi)\n" /* jmp_buf[4] = r12 */
		"movq %%r13, 40(%%rdi)\n" /* jmp_buf[5] = r12 */
		"movq %%r14, 48(%%rdi)\n" /* jmp_buf[6] = r12 */
		"movq %%r15, 56(%%rdi)\n" /* jmp_buf[7] = r12 */
		"xor %%rax, %%rax\n" /* return 0 */
		"retq"
		:::"memory"
	);
}

__attribute__((naked))
__attribute__((noreturn))
void longjmp(jmp_buf env, int val) {
	asm volatile (
		"movq 0(%%rdi),  %%rsp\n"
		"movq 8(%%rdi),  %%rbp\n"
		"movq 24(%%rdi), %%rbx\n"
		"movq 32(%%rdi), %%r12\n"
		"movq 40(%%rdi), %%r13\n"
		"movq 48(%%rdi), %%r14\n"
		"movq 56(%%rdi), %%r15\n"
		"movq %%rsi, %%rax\n"
		"jmpq *16(%%rdi)\n"
		:::"memory"
	);
}

