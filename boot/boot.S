.section .text
.code16
.global _start

_start:
    ljmp $0x0000, $boot_entry

boot_entry:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    cld
    sti

    movb %dl, boot_drive
    movw $boot_message, %si
    call print_string

    movb boot_drive, %dl
    movw $0x55aa, %bx
    movb $0x41, %ah
    int $0x13
    jc lba_error
    cmpw $0xaa55, %bx
    jne lba_error
    testw $1, %cx
    jz lba_error

    movb boot_drive, %dl
    movw $disk_packet, %si
    movb $0x42, %ah
    int $0x13
    jc disk_error

    ljmp $0x1000, $0x0000

lba_error:
    movw $lba_message, %si
    jmp boot_error

disk_error:
    movw $disk_message, %si

boot_error:
    call print_string
halt:
    cli
    hlt
    jmp halt

print_string:
    lodsb
    testb %al, %al
    jz print_done
    movb $0x0e, %ah
    movw $0x0007, %bx
    int $0x10
    jmp print_string
print_done:
    ret

boot_message:
    .asciz "TexugOS: BIOS boot\r\n"
lba_message:
    .asciz "TexugOS: BIOS LBA extensions unavailable\r\n"
disk_message:
    .asciz "TexugOS: payload read failed\r\n"

.align 4
boot_drive:
    .byte 0

disk_packet:
    .byte 0x10, 0x00
    .word 96
    .word 0x0000
    .word 0x1000
    .quad 1

.org 510
.word 0xaa55
