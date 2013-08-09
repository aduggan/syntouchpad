ANDROID_NDK = /home/aduggan/android-ndk-r8c
ANDROID_TOOLCHAIN = $(ANDROID_NDK)/toolchains/x86-4.4.3/prebuilt/linux-x86
ANDROID_PLATFORM = android-14
CC = $(ANDROID_TOOLCHAIN)/bin/i686-linux-android-gcc
LD = $(ANDROID_TOOLCHAIN)/bin/i686-linux-android-ld
STRIP = $(ANDROID_TOOLCHAIN)/bin/i686-linux-android-strip
INCLUDES = -I$(ANDROID_NDK)/platforms/$(ANDROID_PLATFORM)/arch-x86/usr/include -I.
LDFLAGS = -L$(ANDROID_NDK)/platforms/$(ANDROID_PLATFORM)/arch-x86/usr/lib --dynamic-linker /system/bin/linker -nostdlib -rpath /system/lib
LIBS = -lc $(ANDROID_NDK)/platforms/$(ANDROID_PLATFORM)/arch-x86/usr/lib/crtbegin_dynamic.o
CFLAGS = -g -Wall -fno-short-enums $(INCLUDES)
SYNTOUCHPADSRC = syntouchpad.c
SYNTOUCHPADOBJ = $(SYNTOUCHPADSRC:.c=.o)

all: syntouchpad

syntouchpad: $(LIBNAME) $(SYNTOUCHPADOBJ)
	$(LD) $(LDFLAGS) $(SYNTOUCHPADOBJ) -o syntouchpad $(LIBS)
	$(STRIP) syntouchpad


clean:
	rm -f $(SYNTOUCHPADOBJ) syntouchpad
