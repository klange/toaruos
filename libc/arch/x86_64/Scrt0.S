.global _start

_start:
  movq main@GOTPCREL(%rip), %rcx
  and $0xFFFFFFFFFFFFFFF0, %rsp
  call __libc_start_main
