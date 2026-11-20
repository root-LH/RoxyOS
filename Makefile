QEMU    = qemu-system-i386
GDB     = gdb

CC      = gcc
LD      = ld
ASM     = nasm
OBJCOPY = objcopy
AR      = ar

BUILD = build

# 디스크 이미지 크기. 섹터 2048부터 끝까지가 파일 시스템 (RXFS)
IMG_SIZE = 8M

# 커널/유저 공통. -MMD -MP: 헤더 의존성을 .d 파일로 생성
BASE_CFLAGS = -m32 \
              -ffreestanding \
              -fno-pie \
              -fno-stack-protector \
              -fno-builtin \
              -fno-asynchronous-unwind-tables \
              -fno-unwind-tables \
              -fno-exceptions \
              -nostdlib \
              -Wall -Wextra \
              -MMD -MP

# -g: kernel.elf에 디버그 정보 (kernel.bin은 objcopy로 빠지므로 크기 영향 없음)
CFLAGS  = $(BASE_CFLAGS) -g
LDFLAGS = -m elf_i386 -T kernel/linker.ld

# ---- Kernel ----
# kernel/ 아래 .c/.asm을 전부 빌드. entry.asm은 _start가 맨 앞에 와야 하므로 따로 처리.
# 같은 이름의 .c/.asm(isr, idt, irq, tss)이 겹치지 않도록 오브젝트 이름에 확장자를 남김.
KERNEL_C     := $(sort $(shell find kernel -name '*.c'))
KERNEL_ASM   := $(sort $(shell find kernel -name '*.asm' ! -path kernel/entry.asm))
KERNEL_ENTRY := $(BUILD)/kernel/entry.asm.o
KERNEL_OBJS  := $(KERNEL_ENTRY) $(patsubst %,$(BUILD)/%.o,$(KERNEL_C) $(KERNEL_ASM))

# ---- User programs (installed into SimpleFS inside os.img) ----
# user/*.c 파일 하나가 프로그램 하나. user/lib/(*.c, syscall.asm)은 libc.a로 묶어서 링크
USER_BUILD      = $(BUILD)/user
USER_PROGS     := $(basename $(notdir $(wildcard user/*.c)))
USER_ELFS       = $(addprefix $(USER_BUILD)/,$(addsuffix .elf,$(USER_PROGS)))
USER_LIBC_OBJS := $(patsubst user/lib/%.c,$(USER_BUILD)/lib/%.o,$(wildcard user/lib/*.c)) \
                  $(USER_BUILD)/lib/syscall.o
USER_LIBC       = $(USER_BUILD)/libc.a
# -nostdinc: 호스트 glibc 헤더 대신 user/include만 사용 (stddef/stdarg/stdint는 gcc 내장 헤더)
# -fno-tree-loop-distribute-patterns: -O2가 memset/memcpy 루프를 자기 자신 호출로 바꾸지 않도록
GCC_INCLUDE    := $(shell $(CC) -print-file-name=include)
USER_CFLAGS     = $(BASE_CFLAGS) -O2 -fno-tree-loop-distribute-patterns \
                  -nostdinc -isystem $(GCC_INCLUDE) -Iuser/include -Ikernel/include/uapi
USER_LDFLAGS    = -m elf_i386 -T user/linker.ld -s

# gdb 포트. 이 PC에서는 기본값 1234가 열리지 않아서 26000을 씀 (make debug GDB_PORT=1234 처럼 변경 가능)
GDB_PORT ?= 26000

QEMU_FLAGS = -drive format=raw,file=$(BUILD)/os.img \
             -no-reboot \
             -serial stdio

.PHONY: all user run debug gdb clean

all: $(BUILD)/os.img

# ---- Pattern rules ----

$(BUILD)/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: %.asm
	@mkdir -p $(@D)
	$(ASM) -f elf32 -g -MD $(@:.o=.d) -MP $< -o $@

$(USER_BUILD)/%.o: user/%.c
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_BUILD)/crt0.o: user/crt0.asm
	@mkdir -p $(@D)
	$(ASM) -f elf32 $< -o $@

$(USER_BUILD)/lib/syscall.o: user/lib/syscall.asm
	@mkdir -p $(@D)
	$(ASM) -f elf32 $< -o $@

$(USER_LIBC): $(USER_LIBC_OBJS)
	@rm -f $@
	$(AR) rcs $@ $^

# crt0.o must come first so _start leads .text
$(USER_BUILD)/%.elf: $(USER_BUILD)/crt0.o $(USER_BUILD)/%.o $(USER_LIBC) user/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_BUILD)/crt0.o $(USER_BUILD)/$*.o $(USER_LIBC)

.SECONDARY: $(USER_ELFS) $(USER_LIBC_OBJS) $(USER_BUILD)/crt0.o $(USER_PROGS:%=$(USER_BUILD)/%.o)

user: $(USER_ELFS)

# ---- Kernel image ----

$(BUILD)/kernel.elf: $(KERNEL_OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

# ---- Bootloader ----

# Stage1이 읽을 커널 섹터 수 (빌드할 때마다 생성)
$(BUILD)/stage1.inc: $(BUILD)/kernel.bin
	@SIZE=$$(stat -c%s $<); \
	echo "%assign KERNEL_SECTORS $$(((SIZE + 511) / 512))" > $@

$(BUILD)/stage1.bin: boot/stage1.asm $(BUILD)/stage1.inc
	$(ASM) -f bin -I$(BUILD)/ $< -o $@

$(BUILD)/stage2.bin: boot/stage2.asm boot/gdt.inc
	@mkdir -p $(@D)
	$(ASM) -f bin $< -o $@

# ---- Disk image ----

$(BUILD)/os.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin \
                 $(USER_ELFS) tools/fsadd.py
	@# 기존 이미지는 파일 시스템을 보존하고, 작으면 IMG_SIZE까지만 늘림 (truncate '>' = 최소 크기)
	truncate -s '>$(IMG_SIZE)' $@
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=2 conv=notrunc
	@for p in $(USER_PROGS); do \
		python3 tools/fsadd.py $@ /bin/$$p $(USER_BUILD)/$$p.elf || exit 1; \
	done

# ---- Run / debug ----

run: all
	$(QEMU) $(QEMU_FLAGS) \
	-d int,cpu_reset \
	-D $(BUILD)/qemu.log

# QEMU가 첫 명령에서 멈추고 :$(GDB_PORT)에서 gdb를 기다림. 다른 터미널에서 `make gdb`
debug: all
	$(QEMU) $(QEMU_FLAGS) -gdb tcp::$(GDB_PORT) -S

gdb:
	$(GDB) $(BUILD)/kernel.elf \
	-ex "target remote :$(GDB_PORT)" \
	-ex "break kernel_main" \
	-ex "continue"

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
