.global _start

_start:
  pop %eax
  push $main
  call pre_main

