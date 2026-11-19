[org 0x7C00]
[bits 16]

%include "boot/stage1.inc"

; ------------------------
; 디스크 레이아웃 (LBA, 512B 섹터)
;   0            : Stage1 (MBR)
;   1            : Stage2
;   2 ~ 1025     : 커널 (최대 KERNEL_MAX_SECTORS)
;   2048 ~       : SimpleFS (kernel/include/simplefs.h)
; ------------------------
STAGE2_LBA          equ 1
STAGE2_SECTORS      equ 1
STAGE2_ADDR         equ 0x8000

KERNEL_LBA          equ 2
KERNEL_SEGMENT      equ 0x1000      ; 물리 0x10000
%assign KERNEL_MAX_SECTORS 1024       ; 512KB -> 0x10000 ~ 0x90000 (EBDA 아래)
CHUNK_SECTORS       equ 64          ; 한 번에 32KB (offset 0이므로 64KB 경계를 넘지 않음)

start:

    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    sti

    ; BIOS가 전달한 부팅 드라이브 번호 저장
    mov [BOOT_DRIVE], dl

    ; ------------------------
    ; INT 13h 확장(LBA) 지원 확인
    ; ------------------------

    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc no_lba
    cmp bx, 0xAA55
    jne no_lba
    test cx, 1              ; bit 0: AH=42h 지원
    jz no_lba

    ; ------------------------
    ; Load Stage2
    ; ------------------------

    mov word [dap.count], STAGE2_SECTORS
    mov word [dap.offset], STAGE2_ADDR
    mov word [dap.segment], 0
    mov dword [dap.lba], STAGE2_LBA
    call read_lba

    ; ------------------------
    ; Load Kernel (CHUNK_SECTORS씩 반복)
    ; ------------------------

    mov word [dap.offset], 0
    mov word [dap.segment], KERNEL_SEGMENT
    mov dword [dap.lba], KERNEL_LBA
    mov si, KERNEL_SECTORS

.kernel_loop:
    test si, si
    jz .kernel_done

    mov ax, si
    cmp ax, CHUNK_SECTORS
    jbe .chunk_ok
    mov ax, CHUNK_SECTORS
.chunk_ok:
    mov [dap.count], ax
    call read_lba

    mov ax, [dap.count]     ; BIOS가 실제로 읽은 섹터 수로 갱신됨
    test ax, ax
    jz disk_error
    sub si, ax
    add [dap.lba], ax
    adc word [dap.lba + 2], 0
    shl ax, 5               ; 섹터 * 512 / 16 = 세그먼트 증가량
    add [dap.segment], ax
    jmp .kernel_loop

.kernel_done:
    mov dl, [BOOT_DRIVE]
    jmp 0x0000:STAGE2_ADDR

; ------------------------
; DAP에 지정된 섹터를 읽음 (실패 시 3번 재시도)
; ------------------------
read_lba:
    mov bx, [dap.count]     ; 실패 시 BIOS가 count를 바꿀 수 있으므로 보관
    mov di, 3
.retry:
    mov [dap.count], bx
    push si
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    mov si, dap
    int 0x13
    pop si
    jnc .ok

    xor ah, ah              ; 드라이브 리셋 후 재시도
    mov dl, [BOOT_DRIVE]
    int 0x13
    dec di
    jnz .retry
    jmp disk_error
.ok:
    ret

no_lba:
    mov si, msg_lba
    jmp print_halt

disk_error:
    mov si, msg_disk

print_halt:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    int 10h
    jmp print_halt
.halt:
    cli
    hlt
    jmp .halt

msg_disk db "Disk Error", 0
msg_lba  db "No LBA", 0

BOOT_DRIVE db 0

align 4
dap:
    db 0x10                 ; DAP 크기
    db 0
.count:   dw 0
.offset:  dw 0
.segment: dw 0
.lba:     dq 0

%if KERNEL_SECTORS > KERNEL_MAX_SECTORS
%error "kernel.bin is larger than KERNEL_MAX_SECTORS"
%endif

times 510-($-$$) db 0
dw 0xAA55
