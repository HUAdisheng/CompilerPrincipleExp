    .section .text.start
    .globl _start
    .type _start, @function

_start:
    la sp, __stack_top
    call main
    call runtime_report_exit
1:
    j 1b
