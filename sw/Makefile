ifneq (${KERNELRELEASE},)

# KERNELRELEASE defined: being compiled as part of the kernel
	obj-m := pvz_driver.o pvz_audio_driver.o

else

# Standalone build: use the kernel build system for the module,
# and gcc for userspace programs.

	KERNEL_SOURCE := /usr/src/linux-headers-$(shell uname -r)
	PWD := $(shell pwd)

default: module pvz test_shapes test_input test_input_devices test_game

module:
	${MAKE} -C ${KERNEL_SOURCE} SUBDIRS=${PWD} modules

clean:
	${MAKE} -C ${KERNEL_SOURCE} SUBDIRS=${PWD} clean
	${RM} pvz test_shapes test_input test_input_devices test_game

# Main game executable
pvz: main.c game.c render.c input.c audio.c pvz.h game.h render.h input.h audio.h pvz_audio_driver.h
	gcc -Wall -O2 -o pvz main.c game.c render.c input.c audio.c -lpthread

# Test programs
test_shapes: test/test_shapes.c pvz.h
	gcc -Wall -O2 -o test_shapes test/test_shapes.c

test_input: test/test_input.c input.h input.c
	gcc -Wall -O2 -o test_input test/test_input.c input.c

test_input_devices: test/test_input_devices.c input.h input.c
	gcc -Wall -O2 -o test_input_devices test/test_input_devices.c input.c

test_game: test/test_game.c game.c game.h
	gcc -Wall -O2 -o test_game test/test_game.c game.c

endif
