# PvZ HPS software Makefile.
#
# Two build modes:
#   1. On-board native build (default): run `make` on the DE1-SoC
#      with its own kernel headers at /usr/src/linux-headers-$(uname -r).
#   2. Cross-compile from a workstation:
#        make CC=arm-linux-gnueabihf-gcc \
#             ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
#             KERNEL_SOURCE=/path/to/arm/kernel/headers
#      CI uses this form; see .github/workflows/build.yml.
#
# Top-level targets:
#   default      - module + pvz + test_shapes + test_input + test_game
#   module       - build pvz_driver.ko via the kernel build system
#   pvz          - the game binary (main.c + game.c + render.c + input.c)
#   test_shapes  - on-board driver/ioctl smoke test
#   test_input   - on-board evdev polling test
#   test_game    - host-compilable game-logic unit tests
#   clean        - remove kernel-module build artifacts and the four
#                  userspace binaries
#
# How it fits in: deploy.sh calls this (then copies the resulting
# artifacts to the board), and the top-level README references it.

ifneq (${KERNELRELEASE},)

# KERNELRELEASE defined: we're being invoked from the kernel build
	obj-m := pvz_driver.o

else

# Standalone build: use the kernel build system for the module, and
# plain gcc for userspace programs.

	KERNEL_SOURCE := /usr/src/linux-headers-$(shell uname -r)
	PWD := $(shell pwd)

default: module pvz test_shapes test_input test_game

module:
	${MAKE} -C ${KERNEL_SOURCE} SUBDIRS=${PWD} modules

clean:
	${MAKE} -C ${KERNEL_SOURCE} SUBDIRS=${PWD} clean
	${RM} pvz test_shapes test_input test_game

# Main game executable
pvz: main.c game.c render.c input.c pvz.h game.h render.h input.h
	gcc -Wall -O2 -o pvz main.c game.c render.c input.c -lpthread

# Test programs
test_shapes: test/test_shapes.c pvz.h
	gcc -Wall -O2 -o test_shapes test/test_shapes.c

test_input: test/test_input.c input.h input.c
	gcc -Wall -O2 -o test_input test/test_input.c input.c

test_game: test/test_game.c game.c game.h
	gcc -Wall -O2 -o test_game test/test_game.c game.c

endif
