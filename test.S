#
# The goal is a small verification code that
# if VM exit happens in one vCPU that does not
# cause halt or block the rest of the vCPU
#
# It starts 2 vCPU with different execution path (thread1/2).
#
.globl _start
    .code16
_start:
    cmp $0, %ax
    je thread1

    cmp $1, %ax
    je thread2

    mov $42, %ax
    jmp exit

thread1:
# Blocks until other thread starts
# Does a simple loop
# Exists
    out %ax, $0x01 # This call waits as long as a 0x04 out is called from other vCPU
    xor %ax, %ax
loop:
    inc %ax
    cmp $5000, %ax
    je exit
    jmp loop

thread2:
# Triggers an infinite loop on host side just after unlocking the 0x01 out call
    out %ax, $0x04 # Unlocks the 0x01 out call, and stuck in infinite loop at host side (effectivly never returns here)

    # This is dead code
    mov $43, %ax
    jmp exit

exit:
    out %ax, $0x03 # Terminates the current vCPU

