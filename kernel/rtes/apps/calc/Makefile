NDK = $(HOME)/android-ndk-r9
NDK_TC = arm-linux-androideabi-4.8
NDK_API = android-14
SYSROOT = $(NDK)/platforms/$(NDK_API)/arch-arm
LIBSTDCPP = $(NDK)/sources/cxx-stl/gnu-libstdc++/4.8

EXP_HDRS = $(HOME)/lab0-cmu/usr/include

CC = $(NDK)/toolchains/$(NDK_TC)/prebuilt/linux-x86_64/bin/arm-linux-androideabi-gcc

INCLUDES = -isystem $(EXP_HDRS) \
           -isystem $(SYSROOT)/usr/include \
           -isystem $(LIBSTDCPP)/include \
           -isystem $(LIBSTDCPP)/include/backward \
           -isystem $(LIBSTDCPP)/libs/armeabi-v7a/include

EXTRA_CFLAGS = --sysroot=$(SYSROOT) \
               -fsigned-char -march=armv7-a -mfloat-abi=softfp -mfpu=vfp \
               -fdata-sections -ffunction-sections -fexceptions -mthumb -fPIC \
               -Wno-psabi -DANDROID -D__ARM_ARCH_5__ -D__ARM_ARCH_5T__ \
               -D__ARM_ARCH_5E__ -D__ARM_ARCH_5TE__ -fomit-frame-pointer \
			   $(INCLUDES)
               
EXTRA_LDFLAGS = --sysroot=$(SYSROOT) \
                -Wl,--gc-sections \
                -L$(LIBSTDCPP)/libs/armeabi-v7a \
                -lgnustl_static -lsupc++

calc: calc.c
	$(CC) $(EXTRA_CFLAGS) $(EXTRA_LDFLAGS) -o calc calc.c

clean:
	rm -f calc