; Mr. Boots - Stage 1
; Find Stage 2 and immediately load it.
[BITS 16]          ; 16-bit Boot Loader
[ORG 0x7C00]       ; Start point

start:
;	jmp loader     ; Jump over the MBR bits
;
;; MBR OEM Parameter Block
;bpbOEM               db "ToAruOS "
;bpbBytesPerSector    dw 512
;bpbSectorsPerCluster db 1
;bpbReservedSectors   dw 1
;bpbNumberoOfFATs     db 2
;bpbRootEntries       dw 224
;bpbTotalSectors      dw 2880
;bpbMedia             db 0xf0
;bpbSectorsPerFAT     dw 9
;bpbSectorsPerTrack   dw 18
;bpbHeadsPerCylinder  dw 2
;bpbHiddenSectors     dd 0
;bpbTotalSectorsBig   dd 0
;bsDriveNumber        db 0
;bsUnused             db 0
;bsExtBootSignature   db 0x29
;bsSerialNumber       dd 0xdeadbeef
;bsVolumeLabel        db "ToAru Boot "
;bsFileSystem         db "FAT12   "
;
;loader:
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
