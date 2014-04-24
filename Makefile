CC = gcc
LD = $(CC)
STRIP = strip
CFLAGS = -Wall
SYNTOUCHPADSRC = syntouchpad.c
SYNTOUCHPADOBJ = $(SYNTOUCHPADSRC:.c=.o)

all: syntouchpad

syntouchpad: $(SYNTOUCHPADOBJ)
	$(LD) $(SYNTOUCHPADOBJ) -o syntouchpad
	$(STRIP) syntouchpad


clean:
	rm -f $(SYNTOUCHPADOBJ) syntouchpad

android:
	ndk-build NDK_APPLICATION_MK=Application.mk

android-clean:
	ndk-build NDK_APPLICATION_MK=Application.mk clean
