; Mr. Boots - Stage 1
; Find Stage 2 and immediately load it.
;
; NOTICE: This stage should be loaded from a partition on
;         an EXT2-only disk without a partition table.
;         If you want to use it with a different set up
;         you need to patch it to include an MBR header
;         and all the other necessary bits.
;
; Part of the ToAruOS Distribution of the ToAru Kernel
;
; NCSA license is available from the root directory of the
; source tree in which this file is shipped.
;
;
[BITS 16]          ; 16-bit Boot Loader
[ORG 0x7C00]       ; Start point

start:
	mov ax, 0x00   ; Initialize data segment
	mov ds, ax     ; ...

	mov si, bmsg   ; Print "Starting..."
	call print     ; ...

	;mov ax, 0x4F02 ; VESA function "Set Video Mode"
	;mov bx, 0x0107 ; 1024x768x256
	;int 0x10       ; BIOS video interrupt

	jmp $          ; sadloop

print:
	mov ah, 0x0E   ; set registers for
	mov bh, 0x00   ; bios video call
	mov bl, 0x07
.next:
	lodsb          ; string stuff
	or al,al       ; if 0...
	jz .return     ; return
	int 0x10       ; print character
	jmp .next      ; continue
.return:
	ret

bmsg db 'Starting...',13,10,0

; Boot magic
times 510-($-$$) db 0
dw 0xAA55
