QEMU = qemu-system-i386

CC      = gcc
LD      = ld
ASM     = nasm
OBJCOPY = objcopy

CFLAGS = -m32 \
          -ffreestanding \
          -fno-pie \
          -fno-stack-protector \
          -fno-builtin \
          -fno-asynchronous-unwind-tables \
          -fno-unwind-tables \
          -fno-exceptions \
          -nostdlib

LDFLAGS = -m elf_i386 \
           -T kernel/linker.ld

BUILD = build

# ---- User programs (installed into SimpleFS inside os.img) ----
USER_BUILD  = $(BUILD)/user
USER_PROGS  = hello count crash
USER_ELFS   = $(addprefix $(USER_BUILD)/,$(addsuffix .elf,$(USER_PROGS)))
USER_CFLAGS = $(CFLAGS) -O2 -Wall -Wextra -Iuser
USER_LDFLAGS = -m elf_i386 -T user/linker.ld -s

all: $(BUILD)/kernel.bin $(BUILD)/os.img

$(BUILD):
	mkdir -p $(BUILD)

$(USER_BUILD):
	mkdir -p $(USER_BUILD)

$(USER_BUILD)/crt0.o: user/crt0.asm | $(USER_BUILD)
	$(ASM) -f elf32 $< -o $@

$(USER_BUILD)/syscall.o: kernel/lib/user/syscall.asm | $(USER_BUILD)
	$(ASM) -f elf32 $< -o $@

$(USER_BUILD)/%.o: user/%.c user/ulib.h | $(USER_BUILD)
	$(CC) $(USER_CFLAGS) -c $< -o $@

# crt0.o must come first so _start leads .text
$(USER_BUILD)/%.elf: $(USER_BUILD)/crt0.o $(USER_BUILD)/%.o $(USER_BUILD)/ulib.o \
                     $(USER_BUILD)/syscall.o user/linker.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(filter %.o,$^)

.SECONDARY: $(USER_ELFS)

user: $(USER_ELFS)

$(BUILD)/stage1.bin: boot/stage1.asm $(BUILD)/kernel.bin | $(BUILD)
	@SIZE=$$(stat -c%s $(BUILD)/kernel.bin); \
	SECTORS=$$(((SIZE+511)/512)); \
	echo "%assign KERNEL_SECTORS $$SECTORS" > boot/stage1.inc
	$(ASM) -f bin $< -o $@

$(BUILD)/stage2.bin: boot/stage2.asm boot/gdt.inc | $(BUILD)
	$(ASM) -f bin $< -o $@

$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/vga.o: kernel/drivers/vga.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/console.o: kernel/drivers/console.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/serial.o: kernel/drivers/serial.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/idt.o: kernel/arch/x86/idt.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/idtasm.o: kernel/arch/x86/idt.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/io.o: kernel/arch/x86/io.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/irqasm.o: kernel/arch/x86/irq.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/isr.o: kernel/arch/x86/isr.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/isr128.o: kernel/arch/x86/isr128.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/tssasm.o: kernel/arch/x86/tss.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/process_switch.o: kernel/arch/x86/process_switch.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/process_enter_user.o: kernel/arch/x86/process_enter_user.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/paging_asm.o: kernel/arch/x86/paging_asm.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/isrc.o: kernel/arch/x86/isr.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pic.o: kernel/arch/x86/pic.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/irq.o: kernel/arch/x86/irq.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pit.o: kernel/arch/x86/pit.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ata.o: kernel/arch/x86/ata.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/tss.o: kernel/arch/x86/tss.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gdt.o: kernel/arch/x86/gdt.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/simplefs.o: kernel/fs/simplefs.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/syscall_user.o: kernel/lib/user/syscall.asm
	$(ASM) -f elf32 $< -o $@

$(BUILD)/string.o: kernel/libk/string.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/memory.o: kernel/libk/memory.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/printk.o: kernel/libk/printk.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/panic.o: kernel/libk/panic.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/timer.o: kernel/timer.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keyboard.o: kernel/keyboard.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/shell.o: kernel/shell.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vfs.o: kernel/vfs.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/elf.o: kernel/elf_loader.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/syscall.o: kernel/syscall.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fd.o: kernel/fd.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/process.o: kernel/process.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/scheduler.o: kernel/scheduler.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/idle.o: kernel/idle.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/paging.o: kernel/paging.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.o: kernel/kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: \
    $(BUILD)/entry.o \
    $(BUILD)/kernel.o \
	$(BUILD)/vga.o \
	$(BUILD)/idt.o \
	$(BUILD)/idtasm.o \
	$(BUILD)/isr.o \
	$(BUILD)/isr128.o \
	$(BUILD)/isrc.o \
	$(BUILD)/string.o \
	$(BUILD)/memory.o \
	$(BUILD)/printk.o \
	$(BUILD)/console.o \
	$(BUILD)/serial.o \
	$(BUILD)/panic.o \
	$(BUILD)/pic.o \
	$(BUILD)/io.o \
	$(BUILD)/irqasm.o \
	$(BUILD)/irq.o \
	$(BUILD)/pit.o \
	$(BUILD)/timer.o \
	$(BUILD)/keyboard.o \
	$(BUILD)/shell.o \
	$(BUILD)/vfs.o \
	$(BUILD)/ata.o \
	$(BUILD)/simplefs.o \
	$(BUILD)/elf.o \
	$(BUILD)/syscall.o \
	$(BUILD)/tss.o \
	$(BUILD)/gdt.o \
	$(BUILD)/tssasm.o \
	$(BUILD)/syscall_user.o \
	$(BUILD)/fd.o \
	$(BUILD)/process.o \
	$(BUILD)/process_switch.o \
	$(BUILD)/scheduler.o \
	$(BUILD)/idle.o \
	$(BUILD)/process_enter_user.o \
	$(BUILD)/paging_asm.o \
	$(BUILD)/paging.o \
	$(BUILD)/kernel.o | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/stage1.inc: $(BUILD)/kernel.bin | $(BUILD)
	@SECTORS=$$(($(shell stat -c%s $(BUILD)/kernel.bin)+511)); \
	SECTORS=$$((SECTORS/512)); \
	echo "%assign KERNEL_SECTORS $$SECTORS" > boot/stage1.inc

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf | $(BUILD)
	$(OBJCOPY) -O binary $< $@

$(BUILD)/os.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin \
                 $(USER_ELFS) tools/fsadd.py
	@if [ ! -f $@ ]; then \
		dd if=/dev/zero of=$@ bs=512 count=4096; \
	fi
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=2 conv=notrunc
	@for p in $(USER_PROGS); do \
		python3 tools/fsadd.py $@ $$p $(USER_BUILD)/$$p.elf || exit 1; \
	done

run: all
	$(QEMU) \
	-drive format=raw,file=$(BUILD)/os.img \
	-no-reboot \
	-serial stdio \
	-d int,cpu_reset \
	-D $(BUILD)/qemu.log

clean:
	rm -rf $(BUILD)