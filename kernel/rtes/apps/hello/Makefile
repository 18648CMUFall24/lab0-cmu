# Define NDK path and toolchain
NDK = $(HOME)/android-ndk-r9
NDK_TC = arm-linux-androideabi-4.8
NDK_API = android-14
SYSROOT = $(NDK)/platforms/$(NDK_API)/arch-arm

# Exported kernel headers
EXP_HDRS = $(HOME)/kernel/usr/include

# Cross-compiler
CC = $(NDK)/toolchains/$(NDK_TC)/prebuilt/linux-x86_64/bin/arm-linux-androideabi-gcc

# Extra compiler and linker flags
EXTRA_CFLAGS = --sysroot=$(SYSROOT) -I$(EXP_HDRS)
EXTRA_LDFLAGS = --sysroot=$(SYSROOT)

# Build rule
hello: hello.c
	$(CC) -o hello $(EXTRA_CFLAGS) $(EXTRA_LDFLAGS) hello.c
