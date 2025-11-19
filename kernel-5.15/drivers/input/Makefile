# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the input core drivers.
#

# Each configuration option enables a list of files.

obj-$(CONFIG_INPUT)		+= input-core.o
input-core-y := input.o input-compat.o input-mt.o input-poller.o ff-core.o
input-core-y += touchscreen.o

obj-$(CONFIG_INPUT_FF_MEMLESS)	+= ff-memless.o
obj-$(CONFIG_INPUT_SPARSEKMAP)	+= sparse-keymap.o
obj-$(CONFIG_INPUT_MATRIXKMAP)	+= matrix-keymap.o

obj-$(CONFIG_INPUT_LEDS)	+= input-leds.o
obj-$(CONFIG_INPUT_MOUSEDEV)	+= mousedev.o
obj-$(CONFIG_INPUT_JOYDEV)	+= joydev.o
obj-$(CONFIG_INPUT_EVDEV)	+= evdev.o
obj-$(CONFIG_INPUT_EVBUG)	+= evbug.o

obj-$(CONFIG_INPUT_KEYBOARD)	+= keyboard/
obj-$(CONFIG_INPUT_MOUSE)	+= mouse/
obj-$(CONFIG_INPUT_JOYSTICK)	+= joystick/
obj-$(CONFIG_INPUT_TABLET)	+= tablet/
obj-$(CONFIG_INPUT_TOUCHSCREEN)	+= touchscreen/
obj-$(CONFIG_INPUT_TOUCHSCREEN)	+= fingerprint/
obj-$(CONFIG_INPUT_MISC)	+= misc/
obj-$(CONFIG_SEC_INPUT_BOOSTER) += input_booster/

obj-$(CONFIG_INPUT_APMPOWER)	+= apm-power.o

obj-$(CONFIG_RMI4_CORE)		+= rmi4/

obj-$(CONFIG_SX937X_SAR)     += sar/sx937x/sx937x.o
obj-$(CONFIG_STK501XX_SAR)     += sar/stk501xx/stk501xx.o
ccflags-y += -DSTK_BUS_I2C
ccflags-y += -DSTK_GPIO_MTK
ccflags-y += -DSTK_MTK
ccflags-y += -DSTK_RETRY_I2C
