# x86 Assembly w/ libc
.global fprintf # libc export
.global stdout  # libc export

.global main # our exported main function, called by C runtime

main:
	push $world
	push $hello
	push stdout
	call fprintf # fprintf(stdout, "Hello, %s!\n", "world");
	pop %eax
	pop %eax
	pop %eax

	mov $0, %eax
	ret

hello:
	.asciz "Hello, %s!\n"

world:
	.asciz "world"
