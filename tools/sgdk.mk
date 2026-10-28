# Minimal SGDK build for Linux with Debian m68k-linux-gnu cross gcc (no LTO)
GDK ?= /workspace/sgdk
P := m68k-linux-gnu-
CC := $(P)gcc-14
OBJCPY := $(P)objcopy
OUT := out
SRC_C := $(filter-out src/rom_header.c,$(wildcard src/*.c))
RES := $(wildcard res/*.res)
OBJS := $(addprefix $(OUT)/,$(RES:.res=.o) $(SRC_C:.c=.o))
FLAGS := -I. -Iinc -Ires -Isrc -I$(OUT)/res -isystem$(GDK)/inc -isystem$(GDK)/res -DSGDK_GCC -m68000 -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -O2 -fomit-frame-pointer
AFLAGS := -x assembler-with-cpp -Wa,--register-prefix-optional,--bitwise-or $(FLAGS)
all: $(OUT)/rom.bin
$(OUT)/rom.bin: $(OUT)/rom.out
	$(OBJCPY) -O binary $< $@
	java -jar $(GDK)/bin/sizebnd.jar $@ -sizealign 131072 -checksum
$(OUT)/rom.out: $(OUT)/sega.o $(OBJS)
	$(CC) -m68000 -n -T $(GDK)/md.ld -nostdlib $^ /workspace/mdtools/lib/libmd.a /workspace/sgdk/lib/libgcc.a -o $@ -Wl,--gc-sections -Wl,--build-id=none
$(OUT)/sega.o: out/rom_header.bin
	cp $(GDK)/src/boot/sega.s $(OUT)/sega.s
	$(CC) $(AFLAGS) -c $(OUT)/sega.s -o $@
out/rom_header.bin: src/boot/rom_header.c
	mkdir -p $(OUT)
	$(CC) $(FLAGS) -c $< -o $(OUT)/rom_header.o
	$(OBJCPY) -O binary $(OUT)/rom_header.o $@
$(OUT)/res/%.o: res/%.res $(wildcard res/*)
	mkdir -p $(OUT)/res
	java -jar $(GDK)/bin/rescomp.jar $< $(OUT)/res/$*.s
	cp $(OUT)/res/$*.h res/$*.h
	$(CC) $(AFLAGS) -c $(OUT)/res/$*.s -o $@
$(OUT)/src/%.o: src/%.c $(wildcard src/*.h) $(addprefix $(OUT)/,$(RES:.res=.o))
	mkdir -p $(OUT)/src
	$(CC) $(FLAGS) -c $< -o $@
clean:
	rm -rf out
