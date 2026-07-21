/* Minimal Android/Bionic i386 jump-buffer ABI for translated ELF code.
   Bionic's legacy x86 layout stores return address, EBX, ESP, EBP, ESI, EDI,
   and an optional signal-mask word in the first seven 32-bit slots. */

    .text
    .p2align 4

    .globl _shim_bionic_setjmp
_shim_bionic_setjmp:
    movl 4(%esp), %eax
    movl 0(%esp), %edx
    movl %edx, 0(%eax)
    movl %ebx, 4(%eax)
    movl %esp, 8(%eax)
    movl %ebp, 12(%eax)
    movl %esi, 16(%eax)
    movl %edi, 20(%eax)
    movl $0, 24(%eax)
    xorl %eax, %eax
    ret

    .globl _shim_bionic_sigsetjmp
_shim_bionic_sigsetjmp:
    movl 4(%esp), %ecx
    movl 8(%esp), %eax
    movl %eax, 28(%ecx)
    movl 0(%esp), %edx
    movl %edx, 0(%ecx)
    movl %ebx, 4(%ecx)
    movl %esp, 8(%ecx)
    movl %ebp, 12(%ecx)
    movl %esi, 16(%ecx)
    movl %edi, 20(%ecx)
    movl $0, 24(%ecx)
    xorl %eax, %eax
    ret

    .globl _shim_bionic_longjmp
_shim_bionic_longjmp:
    movl 4(%esp), %edx
    movl 8(%esp), %eax
    movl 0(%edx), %ecx
    movl 4(%edx), %ebx
    movl 8(%edx), %esp
    movl 12(%edx), %ebp
    movl 16(%edx), %esi
    movl 20(%edx), %edi
    testl %eax, %eax
    jnz 1f
    incl %eax
1:
    movl %ecx, 0(%esp)
    ret

