# ============================================================================
# Oh-my-os — 顶层 Makefile (M1: Bootloader + Protected Mode)
# ============================================================================

# ---- 工具链 ----
ASM      = as
CXX      = g++
LD       = ld
OBJCOPY  = objcopy
QEMU     = qemu-system-i386

# ---- 编译标志 ----
ASMFLAGS = --32

CXXFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
           -nostartfiles -nodefaultlibs \
           -fno-exceptions -fno-rtti \
           -Wall -Wextra \
           -Iinclude \
           -std=c++17

# ---- 目标 ----
BUILD_DIR  = build

# Bootloader
MBR_SRC    = boot/mbr.S
MBR_ELF    = $(BUILD_DIR)/mbr.elf
MBR_BIN    = $(BUILD_DIR)/mbr.bin

STAGE2_SRC = boot/stage2.S
STAGE2_ELF = $(BUILD_DIR)/stage2.elf
STAGE2_BIN = $(BUILD_DIR)/stage2.bin

BOOT_IMG   = $(BUILD_DIR)/boot.img

# Kernel
KERNEL_ASM = kernel/arch/x86/boot.S kernel/arch/x86/isr_stubs.S
KERNEL_CC  = kernel/kernel.cc \
             kernel/lib/printk.cc \
             kernel/arch/x86/idt.cc \
             kernel/arch/x86/isr.cc \
             kernel/arch/x86/pic.cc \
             kernel/arch/x86/pit.cc \
             kernel/mm/pmm.cc \
             kernel/mm/vmm.cc \
             kernel/mm/heap.cc \
             kernel/drivers/keyboard/keyboard.cc
KERNEL_OBJ = $(BUILD_DIR)/boot.o \
             $(BUILD_DIR)/kernel.o \
             $(BUILD_DIR)/printk.o \
             $(BUILD_DIR)/isr_stubs.o \
             $(BUILD_DIR)/idt.o \
             $(BUILD_DIR)/isr.o \
             $(BUILD_DIR)/pic.o \
             $(BUILD_DIR)/pit.o \
             $(BUILD_DIR)/pmm.o \
             $(BUILD_DIR)/vmm.o \
             $(BUILD_DIR)/heap.o \
             $(BUILD_DIR)/keyboard.o
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin

# 最终磁盘镜像
DISK_IMG   = $(BUILD_DIR)/disk.img

# ---- 默认目标 ----
.PHONY: all
all: $(DISK_IMG)
	@echo ""
	@echo "  M3 build complete."
	@echo "  Disk image: $(DISK_IMG)"
	@echo "  Test: make qemu"
	@echo ""

# ════════════════════════════════════════════════════════════════════════════
# 磁盘镜像 (MBR + Stage2 + Kernel)
# ════════════════════════════════════════════════════════════════════════════

$(DISK_IMG): $(MBR_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	@echo "[IMAGE] Creating disk image..."
	@mkdir -p $(BUILD_DIR)
	@# 创建空镜像 (4MB)
	@dd if=/dev/zero of=$@ bs=512 count=8192 2>/dev/null
	@# 写入 MBR (Sector 0, 512 bytes)
	@dd if=$(MBR_BIN) of=$@ bs=512 count=1 conv=notrunc 2>/dev/null
	@# 写入 Stage2 (Sectors 1-7, 最多 3584 bytes)
	@dd if=$(STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	@# 写入内核 (Sectors 8+, 最多 64KB)
	@dd if=$(KERNEL_BIN) of=$@ bs=512 seek=8 conv=notrunc 2>/dev/null

# ════════════════════════════════════════════════════════════════════════════
# MBR (Sector 0)
# ════════════════════════════════════════════════════════════════════════════

$(MBR_BIN): $(MBR_ELF)
	@echo "[MBR]   Extracting flat binary..."
	@$(OBJCOPY) -O binary $< $@
	@truncate -s 512 $@
	@echo "        $(MBR_BIN) ($(shell wc -c < $@) bytes)"

$(MBR_ELF): $(MBR_SRC) boot/mbr.ld
	@mkdir -p $(BUILD_DIR)
	@echo "[MBR]   Assembling..."
	@$(ASM) $(ASMFLAGS) -o $(BUILD_DIR)/mbr.o $<
	@$(LD) -m elf_i386 -T boot/mbr.ld -o $@ $(BUILD_DIR)/mbr.o

# ════════════════════════════════════════════════════════════════════════════
# Stage2 (Sectors 1-7)
# ════════════════════════════════════════════════════════════════════════════

$(STAGE2_BIN): $(STAGE2_ELF)
	@echo "[STAGE2] Extracting flat binary..."
	@$(OBJCOPY) -O binary $< $@
	@echo "        $(STAGE2_BIN) ($(shell wc -c < $@) bytes)"

$(STAGE2_ELF): $(STAGE2_SRC) boot/boot.ld
	@mkdir -p $(BUILD_DIR)
	@echo "[STAGE2] Assembling..."
	@$(ASM) $(ASMFLAGS) -o $(BUILD_DIR)/stage2.o $<
	@$(LD) -m elf_i386 -T boot/boot.ld -o $@ $(BUILD_DIR)/stage2.o

# ════════════════════════════════════════════════════════════════════════════
# Kernel
# ════════════════════════════════════════════════════════════════════════════

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "[KERNEL] Extracting flat binary..."
	@$(OBJCOPY) -O binary $< $@
	@echo "        $(KERNEL_BIN) ($(shell wc -c < $@) bytes)"

$(KERNEL_ELF): $(KERNEL_OBJ) scripts/link.ld
	@mkdir -p $(BUILD_DIR)
	@echo "[KERNEL] Linking..."
	@$(LD) -m elf_i386 -T scripts/link.ld -o $@ $(KERNEL_OBJ)

# Kernel assembly
$(BUILD_DIR)/boot.o: kernel/arch/x86/boot.S
	@mkdir -p $(BUILD_DIR)
	@echo "[KERNEL] Assembling $(notdir $<)..."
	@$(ASM) $(ASMFLAGS) -o $@ $<

$(BUILD_DIR)/isr_stubs.o: kernel/arch/x86/isr_stubs.S
	@mkdir -p $(BUILD_DIR)
	@echo "[KERNEL] Assembling $(notdir $<)..."
	@$(ASM) $(ASMFLAGS) -o $@ $<

# Kernel C++ (generic pattern)
$(BUILD_DIR)/%.o: kernel/%.cc
	@mkdir -p $(BUILD_DIR)
	@echo "[KERNEL] Compiling $(notdir $<)..."
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: kernel/lib/%.cc
	@mkdir -p $(BUILD_DIR)
	@echo "[KERNEL] Compiling $(notdir $<)..."
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: kernel/arch/x86/%.cc
	@mkdir -p $(BUILD_DIR)
	@echo "[KERNEL] Compiling $(notdir $<)..."
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: kernel/drivers/keyboard/%.cc
	@mkdir -p $(BUILD_DIR)
	@echo "[KERNEL] Compiling $(notdir $<)..."
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: kernel/mm/%.cc
	@mkdir -p $(BUILD_DIR)
	@echo "[KERNEL] Compiling $(notdir $<)..."
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

# ════════════════════════════════════════════════════════════════════════════
# QEMU
# ════════════════════════════════════════════════════════════════════════════

.PHONY: qemu
qemu: $(DISK_IMG)
	@echo "[QEMU] Booting from disk image..."
	$(QEMU) -drive file=$(DISK_IMG),format=raw,index=0,if=ide

.PHONY: qemu-debug
qemu-debug: $(DISK_IMG)
	@echo "[QEMU] Starting with GDB stub (port 1234)..."
	$(QEMU) -drive file=$(DISK_IMG),format=raw,index=0,if=ide -s -S &
	@sleep 1
	@echo "[GDB]  Connect with: gdb -ex 'target remote :1234'"

# ════════════════════════════════════════════════════════════════════════════
# 烧录
# ════════════════════════════════════════════════════════════════════════════

.PHONY: write-usb
write-usb: $(DISK_IMG)
ifndef USB
	$(error Usage: make write-usb USB=/dev/sdX)
endif
	@echo "WARNING: This will erase $(USB)."
	@read -p "Continue? [y/N] " ans && [ "$$ans" = "y" ]
	sudo dd if=$(DISK_IMG) of=$(USB) bs=4M status=progress conv=fsync
	sync
	@echo "Done."

# ════════════════════════════════════════════════════════════════════════════
# 清理
# ════════════════════════════════════════════════════════════════════════════

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
