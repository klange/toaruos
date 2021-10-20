.code16
main:
	/* fix up code seg */
	ljmp $0x0,$entry

entry:
	/* init data segments */
	xor %ax, %ax
	mov %ax, %ds
	mov %ax, %ss
	/* save boot disk */
	mov %dl, boot_disk
	/* set up stack */
	mov $0x7b00, %ax
	mov %ax, %sp
	/* figure out sector size */
	mov $0x48, %ah
	mov boot_disk, %dl
	mov $drive_params, %si
	int $0x13
	/* figure out first sector of stage 2 */
	mov $0, %edx
	mov $BOOT_FILE_OFFSET, %eax
	mov (drive_params_bps), %ecx
	div %ecx
	mov %eax, dap_lba_low
	mov $BOOT_FILE_SIZE, %eax
	div %ecx
	inc %eax
	mov %ax, dap_sectors
	movl $0x7e00, dap_buffer
	mov $0x42, %ah     /* Extended read */
	mov boot_disk, %dl /* Using our boot disk */
	mov $dap, %si      /* From the DAP below */
	int $0x13

	mov $0, %ax
	mov %ax, %es
	mov %ax, %ds
	/* Now move the rest of our code somewhere low */
	mov $mover, %esi
	mov $0x7b00, %edi
	mov $(mover_end-mover), %ecx
	rep movsb
	mov $0x7b00, %eax
	jmp *%eax

mover:
	mov $0x7e00, %esi
	mov $0x7c00, %edi
	mov $BOOT_FILE_SIZE, %ecx
	rep movsb
	mov $0x7c00, %eax
	jmp *%eax
mover_end:

boot_disk:
	.byte 0

.align 4
.global dap
dap:
	.byte 16
	.byte 0 /* always 0 */
.global dap_sectors
dap_sectors:
	.word 1
.global dap_buffer
dap_buffer:
	.long 0x0
.global dap_lba_low
dap_lba_low:
	.long 0
.global dap_lba_high
dap_lba_high:
	.long 0

.align 4
drive_params:
	.word 0x1A
	.word 0 /* flags */
	.long 0 /* cylinders */
	.long 0 /* heads */
	.long 0 /* sectors */
	.quad 0 /* total sectors */
drive_params_bps:
	.word 0 /* bytes per sector */

.org 510
	.byte 0x55
	.byte 0xaa
