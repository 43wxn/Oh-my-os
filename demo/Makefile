# demo/Makefile — 全链路演示
#
# 用法:
#   make         编译 (GNU as + ld + objcopy)
#   make qemu    QEMU 模拟运行
#   make install USB=/dev/sdX  烧录到 U 盘

AS      = as
LD      = ld
OBJCOPY = objcopy
QEMU    = qemu-system-i386

BOOT_BIN = boot.bin

.PHONY: all
all: $(BOOT_BIN)
	@echo ""
	@echo "============================================"
	@echo "  Build successful!"
	@echo "  Size: $$(wc -c < $(BOOT_BIN)) bytes"
	@echo "============================================"
	@echo ""
	@echo "  Next steps:"
	@echo "    Test in QEMU:    make qemu"
	@echo "    Burn to USB:     sudo dd if=$(BOOT_BIN) of=/dev/sdX bs=512 count=1"
	@echo ""

$(BOOT_BIN): boot.elf
	$(OBJCOPY) -O binary $< $@
	@# Pad to exactly 512 bytes (MBR must be 512B)
	@truncate -s 512 $@

boot.elf: boot.o boot.ld
	$(LD) -m elf_i386 -T boot.ld -o $@ boot.o

boot.o: boot.S
	$(AS) --32 -o $@ $<

# QEMU 模拟
.PHONY: qemu
qemu: $(BOOT_BIN)
	@echo "Starting QEMU..."
	@echo "(Close QEMU window or press Ctrl+C to quit)"
	$(QEMU) -drive file=$(BOOT_BIN),format=raw

# 烧录到 U 盘
.PHONY: install
install: $(BOOT_BIN)
ifndef USB
	$(error Usage: make install USB=/dev/sdX)
endif
	@echo "WARNING: This will erase $(USB)."
	@echo "Continue? [y/N]" && read ans && [ "$$ans" = "y" ]
	sudo dd if=$(BOOT_BIN) of=$(USB) bs=512 count=1 conv=fsync
	sync
	@echo "Done. Insert into laptop and boot from USB."

.PHONY: clean
clean:
	rm -f boot.o boot.elf $(BOOT_BIN)
