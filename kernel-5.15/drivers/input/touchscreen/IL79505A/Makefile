ccflags-$(CONFIG_DRM_MEDIATEK) += -I$(srctree)/drivers/gpu/drm/mediatek/
ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(srctree)/drivers/gpu/drm/mediatek/mediatek_v2/

obj-m +=  ilitek.o
ilitek-y := ilitek_v3.o \
			ilitek_v3_hostdl.o \
			ilitek_v3_ic.o \
			ilitek_v3_mp.o \
			ilitek_v3_node.o \
			ilitek_v3_qcom.o \
			ilitek_v3_spi.o \
			ilitek_v3_touch.o
