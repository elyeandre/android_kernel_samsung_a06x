#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

# Connectivity combo driver
# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
    subdir-ccflags-y += -I$(srctree)/
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/clkbuf/src
    subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat
    subdir-ccflags-y += -I$(srctree)/drivers/mmc/core
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/eccci/
    subdir-ccflags-y += -I$(srctree)/drivers/clk/mediatek/
    subdir-ccflags-y += -I$(srctree)/drivers/pinctrl/mediatek/
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/power_throttling/

    # ---------------------------------------------------------------------------
    # Vendor source paths (relative to repo root, one level above srctree)
    # ---------------------------------------------------------------------------
    PATH_TO_WMT_DRV         = vendor/mediatek/kernel_modules/connectivity/common
    PATH_TO_CONNFEM         = vendor/mediatek/kernel_modules/connectivity/connfem
    PATH_TO_CONNINFRA       = vendor/mediatek/kernel_modules/connectivity/conninfra
    PATH_TO_WLAN_CHR_DRV    = vendor/mediatek/kernel_modules/connectivity/wlan/adaptor
    PATH_TO_WLAN_DRV        = vendor/mediatek/kernel_modules/connectivity/wlan/core/gen4m
    PATH_TO_BT_DRV          = vendor/mediatek/kernel_modules/connectivity/bt
    PATH_TO_GPS_STP         = vendor/mediatek/kernel_modules/connectivity/gps/gps_stp
    PATH_TO_GPS_PWR         = vendor/mediatek/kernel_modules/connectivity/gps/gps_pwr
    PATH_TO_FM_DRV          = vendor/mediatek/kernel_modules/connectivity/fmradio

    # ---------------------------------------------------------------------------
    # Create symlinks into vendor source trees (idempotent: unlink first)
    # ---------------------------------------------------------------------------
    $(shell unlink $(srctree)/$(src)/wmt_drv         2>/dev/null; \
            ln -s $(srctree)/../$(PATH_TO_WMT_DRV)      $(srctree)/$(src)/wmt_drv)
    $(shell unlink $(srctree)/$(src)/connfem         2>/dev/null; \
            ln -s $(srctree)/../$(PATH_TO_CONNFEM)       $(srctree)/$(src)/connfem)
    $(shell unlink $(srctree)/$(src)/conninfra       2>/dev/null; \
            ln -s $(srctree)/../$(PATH_TO_CONNINFRA)     $(srctree)/$(src)/conninfra)
    $(shell unlink $(srctree)/$(src)/wmt_chrdev_wifi 2>/dev/null; \
            ln -s $(srctree)/../$(PATH_TO_WLAN_CHR_DRV) $(srctree)/$(src)/wmt_chrdev_wifi)
    $(shell unlink $(srctree)/$(src)/wlan_drv_gen4m  2>/dev/null; \
            ln -s $(srctree)/../$(PATH_TO_WLAN_DRV)     $(srctree)/$(src)/wlan_drv_gen4m)
    $(shell unlink $(srctree)/$(src)/bt_drv_ext      2>/dev/null; \
            ln -s $(srctree)/../$(PATH_TO_BT_DRV)       $(srctree)/$(src)/bt_drv_ext)
    $(shell unlink $(srctree)/$(src)/gps_stp         2>/dev/null; \
            ln -s $(srctree)/../$(PATH_TO_GPS_STP)      $(srctree)/$(src)/gps_stp)
    $(shell unlink $(srctree)/$(src)/gps_pwr         2>/dev/null; \
            ln -s $(srctree)/../$(PATH_TO_GPS_PWR)      $(srctree)/$(src)/gps_pwr)
    $(shell unlink $(srctree)/$(src)/fm_drv_ext      2>/dev/null; \
            ln -s $(srctree)/../$(PATH_TO_FM_DRV)       $(srctree)/$(src)/fm_drv_ext)

    # ---------------------------------------------------------------------------
    # connadp.ko — connectivity build-in adapter (always built)
    # ---------------------------------------------------------------------------
    MODULE_NAME := connadp
    obj-$(CONFIG_MTK_COMBO) += $(MODULE_NAME).o

    $(MODULE_NAME)-objs += common/connectivity_build_in_adapter.o
    $(MODULE_NAME)-objs += common/wmt_build_in_adapter.o
    $(MODULE_NAME)-objs += power_throttling/adapter.o
    $(MODULE_NAME)-objs += power_throttling/core.o
    ifeq ($(CONFIG_CONN_PWR_DEBUG),y)
        $(MODULE_NAME)-objs += power_throttling/test.o
    endif

    ifeq ($(CONFIG_MTK_COMBO), y)
        ccflags-y += -D CFG_CONNADP_BUILD_IN
    endif

    # ---------------------------------------------------------------------------
    # Core connectivity modules: wmt_drv.ko, connfem.ko, conninfra.ko
    # ---------------------------------------------------------------------------
    obj-$(CONFIG_MTK_COMBO) += wmt_drv/
    obj-$(CONFIG_MTK_COMBO) += connfem/
    obj-$(CONFIG_MTK_COMBO) += conninfra/

    # ---------------------------------------------------------------------------
    # WiFi: wmt_chrdev_wifi.ko + wlan_drv_gen4m_6835.ko
    # ---------------------------------------------------------------------------
    ifeq ($(CONFIG_WLAN_DRV_MODULE),y)
        export CONFIG_MTK_COMBO_WIFI_HIF=axi
        export MTK_COMBO_CHIP=SOC2_1X1
        export WLAN_CHIP_ID=6835
        export MTK_ANDROID_WMT=y
        export MODULE_NAME=wlan_drv_gen4m_6835

        obj-m += wmt_chrdev_wifi/
        obj-m += wlan_drv_gen4m/
    endif

    # ---------------------------------------------------------------------------
    # Bluetooth: bt_drv_connac1x.ko
    # ---------------------------------------------------------------------------
    ifeq ($(CONFIG_MTK_BT_DRV_EXTERNAL),y)
        export BT_PLATFORM=connac1x
        export MTK_PLATFORM=mt6835

        obj-m += bt_drv_ext/mt66xx/wmt/
    endif

    # ---------------------------------------------------------------------------
    # GPS: gps_drv_stp.ko + gps_pwr.ko  (legacy STP path, no conninfra dep)
    # ---------------------------------------------------------------------------
    ifeq ($(CONFIG_MTK_GPS_DRV_EXTERNAL),y)
        obj-m += gps_stp/
        obj-m += gps_pwr/
    endif

    # ---------------------------------------------------------------------------
    # FM Radio: fmradio_drv_mt6631_6635.ko
    # ---------------------------------------------------------------------------
    ifeq ($(CONFIG_MTK_FM_DRV_EXTERNAL),y)
        export CFG_FM_PLAT=mt6631_6635
        export CFG_BUILD_CONNAC2=false

        obj-m += fm_drv_ext/Build/mt6631_6635/
    endif

# Otherwise we were called directly from the command line;
# invoke the kernel build system.
else
    KERNELDIR ?= /lib/modules/$(shell uname -r)/build
    PWD  := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif
