MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/usb_c/tcpc/inc

obj-$(CONFIG_AMAZON_LD) += amzn_ld.o
obj-$(CONFIG_AMAZON_METRICS_STUB) += amzn_metrics_stub.o
obj-$(CONFIG_AMAZON_DSP_FRAMEWORK) += dspframework/
