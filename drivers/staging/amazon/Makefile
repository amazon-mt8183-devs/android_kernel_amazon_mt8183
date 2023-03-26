MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/usb_c/tcpc/inc

obj-$(CONFIG_AMAZON_LD) += amzn_ld.o
obj-$(CONFIG_AMAZON_LD_SWITCH) += amzn_ld_switch_init.o
obj-$(CONFIG_AMZN_RT_TRACE)	+= amzn_rt_trace.o
obj-$(CONFIG_AMAZON_DSP_FRAMEWORK) += dspframework/
