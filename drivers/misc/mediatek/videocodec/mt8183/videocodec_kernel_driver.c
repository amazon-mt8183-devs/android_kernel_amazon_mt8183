/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/mm_types.h>
#include <linux/mm.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
/* #include <mach/irqs.h> */
/* #include <mach/x_define_irq.h> */
#include <linux/wait.h>
#include <linux/proc_fs.h>
#include <linux/semaphore.h>
#include <mt-plat/dma.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include "mt-plat/sync_write.h"
#include <linux/wakelock.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <linux/pm_qos.h>
#include <mmdvfs_pmqos.h>

#ifndef CONFIG_MTK_CLKMGR
#include <linux/clk.h>
#else
#include "mach/mt_clkmgr.h"
#endif

#ifdef CONFIG_MTK_HIBERNATION
#include <mtk_hibernate_dpm.h>
#include <mach/diso.h>
#endif

#include "videocodec_kernel_driver.h"
#include "../videocodec_kernel.h"
#include "smi_public.h"
#include <asm/cacheflush.h>
#include <linux/io.h>
#include <asm/sizes.h>
#include "val_types_private.h"
#include "hal_types_private.h"
#include "val_api_private.h"
#include "val_log.h"
#include "drv_api.h"
#include "smi_public.h"
#ifdef CONFIG_MTK_QOS_SUPPORT
/* #define QOS_DEBUG  pr_debug */
#define QOS_DEBUG(...)
#define VCODEC_DVFS_V2
#else
#define QOS_DEBUG(...)
#endif
#ifdef VCODEC_DVFS_V2
#include <linux/slab.h>
#include "dvfs_v2.h"
#endif
#define DVFS_DEBUG(...)

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#if IS_ENABLED(CONFIG_COMPAT)
#include <linux/uaccess.h>
#include <linux/compat.h>
#endif

#define VDO_HW_WRITE(ptr, data)     mt_reg_sync_writel(data, ptr)
#define VDO_HW_READ(ptr)            readl((void __iomem *)ptr)

#define VCODEC_DEVNAME     "Vcodec"
#define VCODEC_DEVNAME2     "Vcodec2"
#define VCODEC_DEV_MAJOR_NUMBER 160   /* 189 */
/* #define VENC_USE_L2C */

static dev_t vcodec_devno = MKDEV(VCODEC_DEV_MAJOR_NUMBER, 0);
static dev_t vcodec_devno2 = MKDEV(VCODEC_DEV_MAJOR_NUMBER, 1);
static struct cdev *vcodec_cdev;
static struct class *vcodec_class;
static struct device *vcodec_device;
struct pm_qos_request vcodec_qos_request;
struct pm_qos_request vcodec_qos_request2;
struct pm_qos_request vcodec_qos_request_f;
struct pm_qos_request vcodec_qos_request_f2;

static struct cdev *vcodec_cdev2;
static struct class *vcodec_class2;
static struct device *vcodec_device2;

#ifndef CONFIG_MTK_SMI_EXT
static struct clk *clk_MT_CG_SMI_COMMON;      /* MM_DISP0_SMI_COMMON */
static struct clk *clk_MT_CG_GALS_VDEC2MM;   /* CLK_MM_GALS_VDEC2MM */
static struct clk *clk_MT_CG_GALS_VENC2MM;   /* CLK_MM_GALS_VENC2MM */
#endif
static struct clk *clk_MT_CG_VDEC;            /* VDEC */

static struct clk *clk_MT_CG_VENC_VENC;         /* VENC_VENC */

static struct clk *clk_MT_SCP_SYS_VDE;          /* SCP_SYS_VDE */
static struct clk *clk_MT_SCP_SYS_VEN;          /* SCP_SYS_VEN */
static struct clk *clk_MT_SCP_SYS_DIS;          /* SCP_SYS_DIS */

static DEFINE_MUTEX(IsOpenedLock);
static DEFINE_MUTEX(PWRLock);
static DEFINE_MUTEX(VdecHWLock);
static DEFINE_MUTEX(VencHWLock);
static DEFINE_MUTEX(EncEMILock);
static DEFINE_MUTEX(L2CLock);
static DEFINE_MUTEX(DecEMILock);
static DEFINE_MUTEX(DriverOpenCountLock);
static DEFINE_MUTEX(DecHWLockEventTimeoutLock);
static DEFINE_MUTEX(EncHWLockEventTimeoutLock);
static DEFINE_MUTEX(DecPMQoSLock);
static DEFINE_MUTEX(EncPMQoSLock);

static DEFINE_MUTEX(VdecPWRLock);
static DEFINE_MUTEX(VencPWRLock);
static DEFINE_MUTEX(LogCountLock);

static DEFINE_SPINLOCK(DecIsrLock);
static DEFINE_SPINLOCK(EncIsrLock);
static DEFINE_SPINLOCK(LockDecHWCountLock);
static DEFINE_SPINLOCK(LockEncHWCountLock);
static DEFINE_SPINLOCK(DecISRCountLock);
static DEFINE_SPINLOCK(EncISRCountLock);


static VAL_EVENT_T DecHWLockEvent;    /* mutex : HWLockEventTimeoutLock */
static VAL_EVENT_T EncHWLockEvent;    /* mutex : HWLockEventTimeoutLock */
static VAL_EVENT_T DecIsrEvent;    /* mutex : HWLockEventTimeoutLock */
static VAL_EVENT_T EncIsrEvent;    /* mutex : HWLockEventTimeoutLock */
static VAL_INT32_T Driver_Open_Count;         /* mutex : DriverOpenCountLock */
static VAL_UINT32_T gu4PWRCounter;      /* mutex : PWRLock */
static VAL_UINT32_T gu4EncEMICounter;   /* mutex : EncEMILock */
static VAL_UINT32_T gu4DecEMICounter;   /* mutex : DecEMILock */
static VAL_UINT32_T gu4L2CCounter;      /* mutex : L2CLock */
static VAL_BOOL_T bIsOpened = VAL_FALSE;    /* mutex : IsOpenedLock */
static VAL_UINT32_T gu4HwVencIrqStatus; /* hardware VENC IRQ status (VP8/H264) */

static VAL_UINT32_T gu4VdecPWRCounter;  /* mutex : VdecPWRLock */
static VAL_UINT32_T gu4VencPWRCounter;  /* mutex : VencPWRLock */

static VAL_UINT32_T gu4LogCountUser;  /* mutex : LogCountLock */
static VAL_UINT32_T gu4LogCount;

static VAL_UINT32_T gLockTimeOutCount;

static VAL_UINT32_T gu4VdecLockThreadId;

static int gi4DecWaitEMI;

#define USE_WAKELOCK 0

#if USE_WAKELOCK == 1
static struct wake_lock vcodec_wake_lock;
static struct wake_lock vcodec_wake_lock2;
#elif USE_WAKELOCK == 0
static unsigned int is_entering_suspend;
#endif

/* #define VCODEC_DEBUG */
#ifdef VCODEC_DEBUG
#undef VCODEC_DEBUG
#define VCODEC_DEBUG pr_info
#undef MODULE_MFV_LOGD
#define MODULE_MFV_LOGD  pr_info
#else
#define VCODEC_DEBUG(...)
#undef MODULE_MFV_LOGD
#define MODULE_MFV_LOGD(...)
#endif

/* VENC physical base address */
#undef VENC_BASE
#define VENC_BASE       0x17020000
#define VENC_REGION     0x2000

/* VDEC virtual base address */
#define VDEC_BASE_PHY   0x16000000
#define VDEC_REGION     0x29000

#define HW_BASE         0x7FFF000
#define HW_REGION       0x2000

#define INFO_BASE       0x10000000
#define INFO_REGION     0x1000

#if 0
#define VENC_IRQ_STATUS_addr        (VENC_BASE + 0x05C)
#define VENC_IRQ_ACK_addr           (VENC_BASE + 0x060)
#define VENC_MP4_IRQ_ACK_addr       (VENC_BASE + 0x678)
#define VENC_MP4_IRQ_STATUS_addr    (VENC_BASE + 0x67C)
#define VENC_ZERO_COEF_COUNT_addr   (VENC_BASE + 0x688)
#define VENC_BYTE_COUNT_addr        (VENC_BASE + 0x680)
#define VENC_MP4_IRQ_ENABLE_addr    (VENC_BASE + 0x668)

#define VENC_MP4_STATUS_addr        (VENC_BASE + 0x664)
#define VENC_MP4_MVQP_STATUS_addr   (VENC_BASE + 0x6E4)
#endif


#define VENC_IRQ_STATUS_SPS         0x1
#define VENC_IRQ_STATUS_PPS         0x2
#define VENC_IRQ_STATUS_FRM         0x4
#define VENC_IRQ_STATUS_DRAM        0x8
#define VENC_IRQ_STATUS_PAUSE       0x10
#define VENC_IRQ_STATUS_SWITCH      0x20
#define VENC_IRQ_STATUS_VPS         0x80

#if 0
/* VDEC virtual base address */
#define VDEC_MISC_BASE  (VDEC_BASE + 0x0000)
#define VDEC_VLD_BASE   (VDEC_BASE + 0x1000)
#endif

#define DRAM_DONE_POLLING_LIMIT 20000

VAL_ULONG_T KVA_VENC_IRQ_ACK_ADDR, KVA_VENC_IRQ_STATUS_ADDR, KVA_VENC_BASE;
VAL_ULONG_T KVA_VDEC_MISC_BASE, KVA_VDEC_VLD_BASE, KVA_VDEC_BASE, KVA_VDEC_GCON_BASE;
VAL_UINT32_T VENC_IRQ_ID, VDEC_IRQ_ID;


/* #define KS_POWER_WORKAROUND */

/* extern unsigned long pmem_user_v2p_video(unsigned long va); */

#if defined(VENC_USE_L2C)
/* extern int config_L2(int option); */
#endif

#define VCODEC_DEBUG_SYS
#ifdef VCODEC_DEBUG_SYS
#define vcodec_attr(_name) \
static struct kobj_attribute _name##_attr = {   \
	.attr   = {                                 \
		.name = __stringify(_name),             \
		.mode = 0644,                           \
	},                                          \
	.show   = _name##_show,                     \
	.store  = _name##_store,                    \
}

#include <linux/kobject.h>
#include <linux/sysfs.h>
static struct kobject *vcodec_debug_kobject;
static unsigned int vcodecDebugMode;
#endif

/* disable temporary for alaska DVT verification, smi seems not ready */
#define ENABLE_MMDVFS_VDEC
#ifdef ENABLE_MMDVFS_VDEC
/* <--- MM DVFS related */
#include <mtk_smi.h>
#include <mmdvfs_config_util.h>
#define DROP_PERCENTAGE     50
#define RAISE_PERCENTAGE    85
#define MONITOR_DURATION_MS 4000
#define DVFS_UNREQUEST (-1)
#define DVFS_LOW     MMDVFS_VOLTAGE_LOW
#define DVFS_HIGH    MMDVFS_VOLTAGE_HIGH
#define DVFS_DEFAULT MMDVFS_VOLTAGE_HIGH
#define MONITOR_START_MINUS_1   0
#define SW_OVERHEAD_MS 1
#define PAUSE_DETECTION_GAP     200
#define PAUSE_DETECTION_RATIO   2
static VAL_BOOL_T   gMMDFVFSMonitorStarts = VAL_FALSE;
static VAL_BOOL_T   gFirstDvfsLock = VAL_FALSE;
static VAL_UINT32_T gMMDFVFSMonitorCounts;
static VAL_TIME_T   gMMDFVFSMonitorStartTime;
static VAL_TIME_T   gMMDFVFSLastLockTime;
static VAL_TIME_T   gMMDFVFSLastUnlockTime;
static VAL_TIME_T   gMMDFVFSMonitorEndTime;
static VAL_UINT32_T gHWLockInterval;
static VAL_INT32_T  gHWLockMaxDuration;
static VAL_UINT32_T gHWLockPrevInterval;
static VAL_UINT32_T gMMDFVSCurrentVoltage = DVFS_UNREQUEST;
static VAL_INT32_T gVDECBWRequested;
static VAL_INT32_T gVENCBWRequested;
static VAL_INT32_T gVDECLevel;
static VAL_UINT32_T gVDECFreq[2] = {450, 312};
static VAL_UINT32_T gVDECFrmTRAVC[4] = {4, 6, 10, 3}; /* /3 for real ratio */
static VAL_UINT32_T gVDECFrmTRHEVC[4] = {3, 5, 13, 3};
static VAL_UINT32_T gVDECFrmTRMP2_4[5] = {5, 9, 10, 20, 3}; /* 3rd element for VP mode */
static u32 dec_step_size;
static u32 enc_step_size;
static u64 g_dec_freq_steps[MAX_FREQ_STEP];
static u64 g_enc_freq_steps[MAX_FREQ_STEP];
#ifdef VCODEC_DVFS_V2
static struct codec_history *dec_hists;
static struct codec_job *dec_jobs;
static DEFINE_MUTEX(VdecDVFSLock);
static struct codec_history *enc_hists;
static struct codec_job *enc_jobs;
static DEFINE_MUTEX(VencDVFSLock);
#endif
static int venc_enableIRQ(VAL_HW_LOCK_T *prHWLock);
static int venc_disableIRQ(VAL_HW_LOCK_T *prHWLock);

VAL_UINT32_T TimeDiffMs(VAL_TIME_T timeOld, VAL_TIME_T timeNew)
{
	/* pr_debug ("@@ timeOld(%d, %d), timeNew(%d, %d)", */
	/* timeOld.u4Sec, timeOld.u4uSec, timeNew.u4Sec, timeNew.u4uSec); */
	return ((((timeNew.u4Sec - timeOld.u4Sec) * 1000000) + timeNew.u4uSec) - timeOld.u4uSec) / 1000;
}

/* raise/drop voltage */
void SendDvfsRequest(int level)
{
	int ret = 0;

	if (level == MMDVFS_VOLTAGE_LOW) {
		MODULE_MFV_LOGD("[VCODEC][MMDVFS_VDEC] SendDvfsRequest(MMDVFS_FINE_STEP_OPP3)");
#ifdef CONFIG_MTK_SMI_EXT
		ret = mmdvfs_set_fine_step(SMI_BWC_SCEN_VP, MMDVFS_FINE_STEP_OPP3);
		gVDECLevel = 1;
#endif
		gMMDFVSCurrentVoltage = MMDVFS_VOLTAGE_LOW;
	} else if (level == MMDVFS_VOLTAGE_HIGH) {
		MODULE_MFV_LOGD("[VCODEC][MMDVFS_VDEC] SendDvfsRequest(MMDVFS_FINE_STEP_OPP0)");
#ifdef CONFIG_MTK_SMI_EXT
		ret = mmdvfs_set_fine_step(SMI_BWC_SCEN_VP, MMDVFS_FINE_STEP_OPP0);
		gVDECLevel = 0;
#endif
		gMMDFVSCurrentVoltage = MMDVFS_VOLTAGE_HIGH;
	}  else if (level == DVFS_UNREQUEST) {
		MODULE_MFV_LOGD("[VCODEC][MMDVFS_VDEC] SendDvfsRequest(MMDVFS_FINE_STEP_UNREQUEST)");
#ifdef CONFIG_MTK_SMI_EXT
		ret = mmdvfs_set_fine_step(SMI_BWC_SCEN_VP, MMDVFS_FINE_STEP_UNREQUEST);
#endif
		gMMDFVSCurrentVoltage = DVFS_UNREQUEST;
	} else {
		MODULE_MFV_LOGD("[VCODEC][MMDVFS_VDEC] OOPS: level = %d\n", level);
	}

	if (ret != 0) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][MMDVFS_VDEC] OOPS: mmdvfs_set_fine_step error!");
	}
}

void VdecDvfsBegin(void)
{
	gMMDFVFSMonitorStarts = VAL_TRUE;
	gMMDFVFSMonitorCounts = 0;
	gHWLockInterval = 0;
	gFirstDvfsLock = VAL_TRUE;
	gHWLockMaxDuration = 0;
	gHWLockPrevInterval = 999999;
	MODULE_MFV_LOGD("[VCODEC][MMDVFS_VDEC] VdecDvfsBegin");
	/* eVideoGetTimeOfDay(&gMMDFVFSMonitorStartTime, sizeof(VAL_TIME_T)); */
}

VAL_UINT32_T VdecDvfsGetMonitorDuration(void)
{
	eVideoGetTimeOfDay(&gMMDFVFSMonitorEndTime, sizeof(VAL_TIME_T));
	return TimeDiffMs(gMMDFVFSMonitorStartTime, gMMDFVFSMonitorEndTime);
}

void VdecDvfsEnd(int level)
{
	pr_debug("[VCODEC][MMDVFS_VDEC] VdecDVFS monitor %dms, decoded %d frames\n",
		 MONITOR_DURATION_MS,
		 gMMDFVFSMonitorCounts);
	pr_debug("[VCODEC][MMDVFS_VDEC] total time %d, max duration %d, target lv %d\n",
		 gHWLockInterval,
		 gHWLockMaxDuration,
		 level);
	gMMDFVFSMonitorStarts = VAL_FALSE;
	gMMDFVFSMonitorCounts = 0;
	gHWLockInterval = 0;
	gHWLockMaxDuration = 0;
}

VAL_UINT32_T VdecDvfsStep(void)
{
	VAL_UINT32_T _diff = 0;

	eVideoGetTimeOfDay(&gMMDFVFSLastUnlockTime, sizeof(VAL_TIME_T));
	_diff = TimeDiffMs(gMMDFVFSLastLockTime, gMMDFVFSLastUnlockTime);
	if (_diff > gHWLockMaxDuration) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		gHWLockMaxDuration = _diff;
	}
	gHWLockInterval += (_diff + SW_OVERHEAD_MS);
	return _diff;
}

void VdecDvfsAdjustment(void)
{
	VAL_UINT32_T _monitor_duration = 0;
	VAL_UINT32_T _diff = 0;
	VAL_UINT32_T _perc = 0;

	if (gMMDFVFSMonitorStarts == VAL_TRUE && gMMDFVFSMonitorCounts > MONITOR_START_MINUS_1) {
		_monitor_duration = VdecDvfsGetMonitorDuration();
		if (_monitor_duration < MONITOR_DURATION_MS) {
			_diff = VdecDvfsStep();
			MODULE_MFV_LOGD("[VCODEC][MMDVFS_VDEC] lock time(%d ms, %d ms), cnt=%d, _monitor_duration=%d\n",
				 _diff, gHWLockInterval, gMMDFVFSMonitorCounts, _monitor_duration);
		} else {
			VdecDvfsStep();
			_perc = (VAL_UINT32_T)(100 * gHWLockInterval / _monitor_duration);
			pr_debug("[VCODEC][MMDVFS_VDEC] DROP_PERCENTAGE = %d, RAISE_PERCENTAGE = %d\n",
				 DROP_PERCENTAGE, RAISE_PERCENTAGE);
			pr_debug("[VCODEC][MMDVFS_VDEC] reset monitor duration (%d ms), percent: %d\n",
				 _monitor_duration, _perc);
			if (_perc < DROP_PERCENTAGE) {
				SendDvfsRequest(DVFS_LOW);
				VdecDvfsEnd(DVFS_LOW);
			} else if (_perc > RAISE_PERCENTAGE) {
				SendDvfsRequest(DVFS_HIGH);
				VdecDvfsEnd(DVFS_HIGH);
			} else {
				VdecDvfsEnd(-1);
			}
		}
	}
	gMMDFVFSMonitorCounts++;
}

void VdecDvfsMonitorStart(void)
{
	VAL_UINT32_T _diff = 0;
	VAL_TIME_T   _now;

	if (gMMDFVFSMonitorStarts == VAL_TRUE) {
		eVideoGetTimeOfDay(&_now, sizeof(VAL_TIME_T));
		_diff = TimeDiffMs(gMMDFVFSLastUnlockTime, _now);
		/* MODULE_MFV_LOGD("[VCODEC][MMDVFS_VDEC] Pause handle prev_diff = %dms, diff = %dms\n", */
		/*		gHWLockPrevInterval, _diff); */
		if (_diff > PAUSE_DETECTION_GAP && _diff > gHWLockPrevInterval * PAUSE_DETECTION_RATIO) {
			/* MODULE_MFV_LOGD("[VCODEC][MMDVFS_VDEC] Pause detected, reset\n"); */
			/* Reset monitoring period if pause is detected */
			SendDvfsRequest(DVFS_HIGH);
			VdecDvfsBegin();
		}
		gHWLockPrevInterval = _diff;
	}
	if (gMMDFVFSMonitorStarts == VAL_FALSE) {
		/* Continuous monitoring */
		VdecDvfsBegin();
	}
	if (gMMDFVFSMonitorStarts == VAL_TRUE) {
		MODULE_MFV_LOGD("[VCODEC][MMDVFS_VDEC] LOCK 1\n");
		if (gMMDFVFSMonitorCounts > MONITOR_START_MINUS_1) {
			if (gFirstDvfsLock == VAL_TRUE) {
				gFirstDvfsLock = VAL_FALSE;
				/* pr_debug("[VCODEC][MMDVFS_VDEC] LOCK 1 start monitor instance = 0x%p\n", */
				/*		grVcodecDecHWLock.pvHandle); */
				eVideoGetTimeOfDay(&gMMDFVFSMonitorStartTime, sizeof(VAL_TIME_T));
			}
			eVideoGetTimeOfDay(&gMMDFVFSLastLockTime, sizeof(VAL_TIME_T));
		}
	}
}
/* ---> */
#endif

void *mt_venc_base_get(void)
{
	return (void *)KVA_VENC_BASE;
}
EXPORT_SYMBOL(mt_venc_base_get);

void *mt_vdec_base_get(void)
{
	return (void *)KVA_VDEC_BASE;
}
EXPORT_SYMBOL(mt_vdec_base_get);
/* void vdec_log_status(void)
*{
*	VAL_UINT32_T u4DataStatusMain = 0;
*	VAL_UINT32_T i = 0;
*
*	for (i = 45; i < 72; i++) {
*		if (i == 45 || i == 46 || i == 52 || i == 58 || i == 59 || i == 61 || i == 62 || i == 63 || i == 71) {
*			u4DataStatusMain = VDO_HW_READ(KVA_VDEC_VLD_BASE+(i*4));
*			MODULE_MFV_PR_INFO("[VCODEC][DUMP] VLD_%d = %x\n", i, u4DataStatusMain);
*		}
*	}
*
*	for (i = 66; i < 80; i++) {
*		u4DataStatusMain = VDO_HW_READ(KVA_VDEC_MISC_BASE+(i*4));
*		MODULE_MFV_PR_INFO("[VCODEC][DUMP] MISC_%d = %x\n", i, u4DataStatusMain);
*	}
*
*}
*/

void vdec_polling_status(void)
{
	VAL_UINT32_T u4DataStatusMain = 0;
	VAL_UINT32_T u4DataStatus = 0;
	VAL_UINT32_T u4CgStatus = 0;
	VAL_UINT32_T u4Counter = 0;

	u4CgStatus = VDO_HW_READ(KVA_VDEC_GCON_BASE);
	u4DataStatusMain = VDO_HW_READ(KVA_VDEC_VLD_BASE+(61*4));

	while ((u4CgStatus != 0) && (u4DataStatusMain & (1<<15)) && ((u4DataStatusMain & 1) != 1)) {
		gi4DecWaitEMI = 1;
		u4CgStatus = VDO_HW_READ(KVA_VDEC_GCON_BASE);
		u4DataStatusMain = VDO_HW_READ(KVA_VDEC_VLD_BASE+(61*4));
		if (u4Counter++ > DRAM_DONE_POLLING_LIMIT) {
			VAL_UINT32_T u4IntStatus = 0;
			VAL_UINT32_T i = 0;

			MODULE_MFV_PR_INFO("[VCODEC][POTENTIAL ERROR] Leftover data access before powering down\n");
			for (i = 45; i < 72; i++) {
				if (i == 45 || i == 46 || i == 52 || i == 58 || i == 59 || i == 61 ||
					i == 62 || i == 63 || i == 71){
					u4IntStatus = VDO_HW_READ(KVA_VDEC_VLD_BASE+(i*4));
					MODULE_MFV_PR_INFO("[VCODEC][DUMP] VLD_%d = %x\n", i, u4IntStatus);
				}
			}

			for (i = 66; i < 80; i++) {
				u4DataStatus = VDO_HW_READ(KVA_VDEC_MISC_BASE+(i*4));
				MODULE_MFV_PR_INFO("[VCODEC][DUMP] MISC_%d = %x\n", i, u4DataStatus);
			}

			smi_debug_bus_hanging_detect_ext2(0x1FF, 1, 0, 1);
			/*mmsys_cg_check(); */

			u4Counter = 0;
			WARN_ON(1);
		}
	}
	gi4DecWaitEMI = 0;
	/* MODULE_MFV_PR_INFO("u4Counter %d\n", u4Counter); */

}

void vdec_power_on(void)
{
	int ret = 0;

	mutex_lock(&VdecPWRLock);
	gu4VdecPWRCounter++;
	mutex_unlock(&VdecPWRLock);
	ret = 0;

#ifdef CONFIG_MTK_QOS_SUPPORT
#ifndef VCODEC_DVFS_V2
	mutex_lock(&DecPMQoSLock);
	QOS_DEBUG("[PMQoS] vdec_power_on set to (0,1) %d, freq = %llu", gVDECLevel, g_dec_freq_steps[gVDECLevel]);
	pm_qos_update_request(&vcodec_qos_request_f, g_dec_freq_steps[gVDECLevel]);
	mutex_unlock(&DecPMQoSLock);
#endif
#endif

	ret = clk_prepare_enable(clk_MT_SCP_SYS_DIS);
	if (ret) {
		/* print error log & error handling */
		pr_debug("[VCODEC][ERROR][vdec_power_on] clk_MT_SCP_SYS_DIS is not enabled, ret = %d\n", ret);
	}

	smi_bus_enable(SMI_LARB_VDECSYS, "VDEC");

	ret = clk_prepare_enable(clk_MT_SCP_SYS_VDE);
	if (ret) {
		/* print error log & error handling */
		pr_debug("[VCODEC][ERROR][vdec_power_on] clk_MT_SCP_SYS_VDE is not enabled, ret = %d\n", ret);
	}

	ret = clk_prepare_enable(clk_MT_CG_VDEC);
	if (ret) {
		/* print error log & error handling */
		pr_debug("[VCODEC][ERROR][vdec_power_on] clk_MT_CG_VDEC is not enabled, ret = %d\n", ret);
	}
}

#ifdef VCODEC_DEBUG_SYS
static ssize_t vcodec_debug_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
#ifdef ENABLE_MMDVFS_VDEC
	VAL_UINT32_T _monitor_duration = 0;
	VAL_UINT32_T _perc = 0;

	if (gMMDFVFSMonitorStarts == VAL_TRUE && gMMDFVFSMonitorCounts > MONITOR_START_MINUS_1) {
		_monitor_duration = VdecDvfsGetMonitorDuration();
		_perc = (VAL_UINT32_T)(100 * gHWLockInterval / _monitor_duration);
		return sprintf(buf, "[MMDVFS_VDEC] drop_thre=%d, raise_thre=%d, vol=%d, percent=%d\n",
				 DROP_PERCENTAGE, RAISE_PERCENTAGE, gMMDFVSCurrentVoltage, _perc);
	} else {
		return sprintf(buf, "End of monitoring interval. Please try again.\n");
	}
#else
	return sprintf(buf, "Not profiling(%d).\n", vcodecDebugMode);
#endif
}

static ssize_t vcodec_debug_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	if (sscanf(buf, "%du", &vcodecDebugMode) == 1) {
		/* Add one line comment to meet coding style */
		MODULE_MFV_LOGD("[VCODEC][vcodec_debug_store] Input is stored\n");
	}
	return count;
}

vcodec_attr(vcodec_debug);

#endif

void vdec_power_off(void)
{
	mutex_lock(&VdecPWRLock);
	if (gu4VdecPWRCounter == 0) {
		MODULE_MFV_LOGD("[VCODEC] gu4VdecPWRCounter = 0\n");
	} else {
		vdec_polling_status();
		gu4VdecPWRCounter--;
		clk_disable_unprepare(clk_MT_CG_VDEC);
		clk_disable_unprepare(clk_MT_SCP_SYS_VDE);

		smi_bus_disable(SMI_LARB_VDECSYS, "VDEC");

		clk_disable_unprepare(clk_MT_SCP_SYS_DIS);
	}
	mutex_unlock(&VdecPWRLock);
	mutex_lock(&DecPMQoSLock);
#ifdef CONFIG_MTK_QOS_SUPPORT
	QOS_DEBUG("[PMQoS] vdec_power_off reset to 0");
	pm_qos_update_request(&vcodec_qos_request, 0);
	gVDECBWRequested = 0;
#endif
#ifdef VCODEC_DVFS_V2
	pm_qos_update_request(&vcodec_qos_request_f, 0);
#endif
	mutex_unlock(&DecPMQoSLock);
}

void venc_power_on(void)
{
	int ret = 0;

	mutex_lock(&VencPWRLock);
	gu4VencPWRCounter++;
	mutex_unlock(&VencPWRLock);
	ret = 0;

	MODULE_MFV_LOGD("[VCODEC] venc_power_on +\n");

	ret = clk_prepare_enable(clk_MT_SCP_SYS_DIS);
	if (ret) {
		/* print error log & error handling */
		pr_debug("[VCODEC][ERROR][venc_power_on] clk_MT_SCP_SYS_DIS is not enabled, ret = %d\n", ret);
	}

	smi_bus_enable(SMI_LARB_VENCSYS, "VENC");

	ret = clk_prepare_enable(clk_MT_SCP_SYS_VEN);
	if (ret) {
		/* print error log & error handling */
		pr_debug("[VCODEC][ERROR][venc_power_on] clk_MT_SCP_SYS_VEN is not enabled, ret = %d\n", ret);
	}

	ret = clk_prepare_enable(clk_MT_CG_VENC_VENC);
	if (ret) {
		/* print error log & error handling */
		pr_debug("[VCODEC][ERROR][venc_power_on] clk_MT_CG_VENC_VENC is not enabled, ret = %d\n", ret);
	}

	MODULE_MFV_LOGD("[VCODEC] venc_power_on -\n");
}

void venc_power_off(void)
{
	mutex_lock(&VencPWRLock);
	if (gu4VencPWRCounter == 0) {
		MODULE_MFV_LOGD("[VCODEC] gu4VencPWRCounter = 0\n");
	} else {
		gu4VencPWRCounter--;
		MODULE_MFV_LOGD("[VCODEC] venc_power_off +\n");

		clk_disable_unprepare(clk_MT_CG_VENC_VENC);
		clk_disable_unprepare(clk_MT_SCP_SYS_VEN);

		smi_bus_disable(SMI_LARB_VENCSYS, "VENC");

		clk_disable_unprepare(clk_MT_SCP_SYS_DIS);

		MODULE_MFV_LOGD("[VCODEC] venc_power_off -\n");
	}
	mutex_unlock(&VencPWRLock);
	mutex_lock(&EncPMQoSLock);
#ifdef CONFIG_MTK_QOS_SUPPORT
	QOS_DEBUG("[PMQoS] venc_power_off reset to 0");
	pm_qos_update_request(&vcodec_qos_request2, 0);
	gVENCBWRequested = 0;
#endif
#ifdef VCODEC_DVFS_V2
	pm_qos_update_request(&vcodec_qos_request_f2, 0);
#endif
	mutex_unlock(&EncPMQoSLock);
}

void vdec_break(void)
{
	unsigned int i;

	/* Step 1: set vdec_break */
	VDO_HW_WRITE(KVA_VDEC_MISC_BASE + 64*4, 0x1);

	/* Step 2: monitor status vdec_break_ok */
	for (i = 0; i < 5000; i++) {
		if ((VDO_HW_READ(KVA_VDEC_MISC_BASE + 65*4) & 0x11) == 0x11) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			break;
		}
	}

	if (i >= 5000) {
		unsigned int j;
		VAL_UINT32_T u4DataStatus = 0;

		pr_info("[VCODEC][POTENTIAL ERROR] Leftover data access before powering down\n");

		for (j = 68; j < 80; j++) {
			u4DataStatus = VDO_HW_READ(KVA_VDEC_MISC_BASE+(j*4));
			pr_info("[VCODEC][DUMP] MISC_%d = 0x%08x", j, u4DataStatus);
		}
		u4DataStatus = VDO_HW_READ(KVA_VDEC_VLD_BASE+(45*4));
		pr_info("[VCODEC][DUMP] VLD_45 = 0x%08x", u4DataStatus);
		u4DataStatus = VDO_HW_READ(KVA_VDEC_VLD_BASE+(46*4));
		pr_info("[VCODEC][DUMP] VLD_46 = 0x%08x", u4DataStatus);
		u4DataStatus = VDO_HW_READ(KVA_VDEC_VLD_BASE+(52*4));
		pr_info("[VCODEC][DUMP] VLD_52 = 0x%08x", u4DataStatus);
		u4DataStatus = VDO_HW_READ(KVA_VDEC_VLD_BASE+(58*4));
		pr_info("[VCODEC][DUMP] VLD_58 = 0x%08x", u4DataStatus);
		u4DataStatus = VDO_HW_READ(KVA_VDEC_VLD_BASE+(59*4));
		pr_info("[VCODEC][DUMP] VLD_59 = 0x%08x", u4DataStatus);
		u4DataStatus = VDO_HW_READ(KVA_VDEC_VLD_BASE+(61*4));
		pr_info("[VCODEC][DUMP] VLD_61 = 0x%08x", u4DataStatus);
		u4DataStatus = VDO_HW_READ(KVA_VDEC_VLD_BASE+(62*4));
		pr_info("[VCODEC][DUMP] VLD_62 = 0x%08x", u4DataStatus);
		u4DataStatus = VDO_HW_READ(KVA_VDEC_VLD_BASE+(63*4));
		pr_info("[VCODEC][DUMP] VLD_63 = 0x%08x", u4DataStatus);
		u4DataStatus = VDO_HW_READ(KVA_VDEC_VLD_BASE+(71*4));
		pr_info("[VCODEC][DUMP] VLD_71 = 0x%08x", u4DataStatus);
		u4DataStatus = VDO_HW_READ(KVA_VDEC_MISC_BASE+(66*4));
		pr_info("[VCODEC][DUMP] MISC_66 = 0x%08x", u4DataStatus);
	}

	/* Step 3: software reset */
	VDO_HW_WRITE(KVA_VDEC_VLD_BASE + 66*4, 0x1);
	VDO_HW_WRITE(KVA_VDEC_VLD_BASE + 66*4, 0x0);
}

void venc_break(void)
{
	unsigned int i;
	VAL_ULONG_T VENC_SW_PAUSE   = KVA_VENC_BASE + 0xAC;
	VAL_ULONG_T VENC_IRQ_STATUS = KVA_VENC_BASE + 0x5C;
	VAL_ULONG_T VENC_SW_HRST_N  = KVA_VENC_BASE + 0xA8;
	VAL_ULONG_T VENC_IRQ_ACK    = KVA_VENC_BASE + 0x60;

	/* Step 1: raise pause hardware signal */
	VDO_HW_WRITE(VENC_SW_PAUSE, 0x1);

	/* Step 2: assume software can only tolerate 5000 APB read time. */
	for (i = 0; i < 5000; i++) {
		if (VDO_HW_READ(VENC_IRQ_STATUS) & 0x10) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			break;
		}
	}

	/* Step 3: Lower pause hardware signal and lower software hard reset signal */
	if (i >= 5000) {
		VDO_HW_WRITE(VENC_SW_PAUSE, 0x0);
		VDO_HW_WRITE(VENC_SW_HRST_N, 0x0);
		VDO_HW_READ(VENC_SW_HRST_N);
	}

	/* Step 4: Lower software hard reset signal and lower pause hardware signal */
	else {
		VDO_HW_WRITE(VENC_SW_HRST_N, 0x0);
		VDO_HW_READ(VENC_SW_HRST_N);
		VDO_HW_WRITE(VENC_SW_PAUSE, 0x0);
	}

	/* Step 5: Raise software hard reset signal */
	VDO_HW_WRITE(VENC_SW_HRST_N, 0x1);
	VDO_HW_READ(VENC_SW_HRST_N);
	/* Step 6: Clear pause status */
	VDO_HW_WRITE(VENC_IRQ_ACK, 0x10);
}

int mt_vdec_runtime_suspend(struct device *dev)
{
	vdec_power_off();
	return 0;
}

int mt_vdec_runtime_resume(struct device *dev)
{
	vdec_power_on();
	return 0;
}

int mt_venc_runtime_suspend(struct device *dev)
{
	venc_power_off();
	return 0;
}

int mt_venc_runtime_resume(struct device *dev)
{
	venc_power_on();
	return 0;
}

void dec_isr(void)
{
	VAL_RESULT_T    eValRet;
	VAL_ULONG_T     ulFlags, ulFlagsISR, ulFlagsLockHW;

	VAL_UINT32_T u4TempDecISRCount = 0;
	VAL_UINT32_T u4TempLockDecHWCount = 0;
	VAL_UINT32_T u4CgStatus = 0;
	VAL_UINT32_T u4DecDoneStatus = 0;

	u4CgStatus = VDO_HW_READ(KVA_VDEC_GCON_BASE);
	if ((u4CgStatus & 0x10) != 0) {
		pr_debug("[VCODEC][ERROR] DEC ISR, VDEC active is not 0x0 (0x%08x)", u4CgStatus);
		return;
	}

	u4DecDoneStatus = VDO_HW_READ(KVA_VDEC_MISC_BASE+0xA4);
	if ((u4DecDoneStatus & (0x1 << 16)) != 0x10000) {
		pr_debug("[VCODEC][ERROR] DEC ISR, Decode done status is not 0x1 (0x%08x)", u4DecDoneStatus);
		return;
	}


	spin_lock_irqsave(&DecISRCountLock, ulFlagsISR);
	gu4DecISRCount++;
	u4TempDecISRCount = gu4DecISRCount;
	spin_unlock_irqrestore(&DecISRCountLock, ulFlagsISR);

	spin_lock_irqsave(&LockDecHWCountLock, ulFlagsLockHW);
	u4TempLockDecHWCount = gu4LockDecHWCount;
	spin_unlock_irqrestore(&LockDecHWCountLock, ulFlagsLockHW);

	if (u4TempDecISRCount != u4TempLockDecHWCount) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		/* pr_debug("[INFO] Dec ISRCount: 0x%x, LockHWCount:0x%x\n",
		 * u4TempDecISRCount, u4TempLockDecHWCount);
		 */
	}

	/* Clear interrupt */
	VDO_HW_WRITE(KVA_VDEC_MISC_BASE+41*4, VDO_HW_READ(KVA_VDEC_MISC_BASE + 41*4) | 0x11);
	VDO_HW_WRITE(KVA_VDEC_MISC_BASE+41*4, VDO_HW_READ(KVA_VDEC_MISC_BASE + 41*4) & ~0x10);


	spin_lock_irqsave(&DecIsrLock, ulFlags);
	eValRet = eVideoSetEvent(&DecIsrEvent, sizeof(VAL_EVENT_T));
	if (eValRet != VAL_RESULT_NO_ERROR) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] ISR set DecIsrEvent error\n");
	}
	spin_unlock_irqrestore(&DecIsrLock, ulFlags);
}


void enc_isr(void)
{
	VAL_RESULT_T  eValRet;
	VAL_ULONG_T ulFlagsISR, ulFlagsLockHW;


	VAL_UINT32_T u4TempEncISRCount = 0;
	VAL_UINT32_T u4TempLockEncHWCount = 0;
	/* ---------------------- */

	spin_lock_irqsave(&EncISRCountLock, ulFlagsISR);
	gu4EncISRCount++;
	u4TempEncISRCount = gu4EncISRCount;
	spin_unlock_irqrestore(&EncISRCountLock, ulFlagsISR);

	spin_lock_irqsave(&LockEncHWCountLock, ulFlagsLockHW);
	u4TempLockEncHWCount = gu4LockEncHWCount;
	spin_unlock_irqrestore(&LockEncHWCountLock, ulFlagsLockHW);

	if (u4TempEncISRCount != u4TempLockEncHWCount) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		/* pr_debug("[INFO] Enc ISRCount: 0x%x, LockHWCount:0x%x\n",
		 * u4TempEncISRCount, u4TempLockEncHWCount);
		 */
	}

	if (grVcodecEncHWLock.pvHandle == 0) {
		pr_debug("[VCODEC][ERROR] NO one Lock Enc HW, please check!!\n");

		/* Clear all status */
		/* VDO_HW_WRITE(KVA_VENC_MP4_IRQ_ACK_ADDR, 1); */
		VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_PAUSE);
		/* VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_DRAM_VP8); */
		VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_SWITCH);
		VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_DRAM);
		VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_SPS);
		VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_PPS);
		VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_FRM);
		VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_VPS);
		return;
	}

	if ((grVcodecEncHWLock.eDriverType == VAL_DRIVER_TYPE_H264_ENC) ||
	(grVcodecEncHWLock.eDriverType == VAL_DRIVER_TYPE_HEVC_ENC)) { /* hardwire */
		gu4HwVencIrqStatus = VDO_HW_READ(KVA_VENC_IRQ_STATUS_ADDR);
		if (gu4HwVencIrqStatus & VENC_IRQ_STATUS_PAUSE) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_PAUSE);
		}
		if (gu4HwVencIrqStatus & VENC_IRQ_STATUS_SWITCH) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_SWITCH);
		}
		if (gu4HwVencIrqStatus & VENC_IRQ_STATUS_DRAM) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_DRAM);
		}
		if (gu4HwVencIrqStatus & VENC_IRQ_STATUS_SPS) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_SPS);
		}
		if (gu4HwVencIrqStatus & VENC_IRQ_STATUS_PPS) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_PPS);
		}
		if (gu4HwVencIrqStatus & VENC_IRQ_STATUS_FRM) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_FRM);
		}
		if (gu4HwVencIrqStatus & VENC_IRQ_STATUS_VPS) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			VDO_HW_WRITE(KVA_VENC_IRQ_ACK_ADDR, VENC_IRQ_STATUS_VPS);
		}
	} else {
		pr_debug("[VCODEC][ERROR] Invalid lock holder driver type = %d\n",
			grVcodecEncHWLock.eDriverType);
	}

	eValRet = eVideoSetEvent(&EncIsrEvent, sizeof(VAL_EVENT_T));
	if (eValRet != VAL_RESULT_NO_ERROR) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] ISR set EncIsrEvent error\n");
	}
}

static irqreturn_t video_intr_dlr(int irq, void *dev_id)
{
	dec_isr();
	return IRQ_HANDLED;
}

static irqreturn_t video_intr_dlr2(int irq, void *dev_id)
{
	enc_isr();
	return IRQ_HANDLED;
}

static long vcodec_lockhw_dec_fail(VAL_HW_LOCK_T rHWLock, VAL_UINT32_T FirstUseDecHW)
{
	pr_debug("[ERROR] VCODEC_LOCKHW, DecHWLockEvent TimeOut, CurrentTID = %d\n", current->pid);
	if (FirstUseDecHW != 1) {
		mutex_lock(&VdecHWLock);
		if (grVcodecDecHWLock.pvHandle == 0) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			pr_debug("[WARNING] VCODEC_LOCKHW, maybe mediaserver restart before, please check!!\n");
		} else {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			pr_debug("[WARNING] VCODEC_LOCKHW, someone use HW, and check timeout value!!\n");
			pr_debug("[WARNING] current owner = 0x%lx, tid = %u, wait EMI status = %d\n",
					(VAL_ULONG_T)grVcodecDecHWLock.pvHandle, gu4VdecLockThreadId, gi4DecWaitEMI);
		}
		mutex_unlock(&VdecHWLock);
	}

	return 0;
}

static long vcodec_lockhw_enc_fail(VAL_HW_LOCK_T rHWLock, VAL_UINT32_T FirstUseEncHW)
{
	pr_debug("[ERROR] VCODEC_LOCKHW EncHWLockEvent TimeOut, CurrentTID = %d\n", current->pid);

	if (FirstUseEncHW != 1) {
		mutex_lock(&VencHWLock);
		if (grVcodecEncHWLock.pvHandle == 0) {
			pr_debug("[WARNING] VCODEC_LOCKHW, maybe mediaserver restart before, please check!!\n");
		} else {
			pr_debug("[WARNING] VCODEC_LOCKHW, someone use HW, and check timeout value!! %d\n",
				 gLockTimeOutCount);
			++gLockTimeOutCount;
			if (gLockTimeOutCount > 30) {
				pr_debug("[ERROR] VCODEC_LOCKHW - ID %d fail\n", current->pid);
				pr_debug("someone locked HW time out more than 30 times 0x%lx,%lx,0x%lx,type:%d\n",
					 (VAL_ULONG_T)grVcodecEncHWLock.pvHandle,
					 pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle),
					 (VAL_ULONG_T)rHWLock.pvHandle,
					 rHWLock.eDriverType);
				gLockTimeOutCount = 0;
				mutex_unlock(&VencHWLock);
				return -EFAULT;
			}

			if (rHWLock.u4TimeoutMs == 0) {
				pr_debug("[ERROR] VCODEC_LOCKHW - ID %d fail\n", current->pid);
				pr_debug("someone locked HW already 0x%lx,%lx,0x%lx,type:%d\n",
					 (VAL_ULONG_T)grVcodecEncHWLock.pvHandle,
					 pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle),
					 (VAL_ULONG_T)rHWLock.pvHandle,
					 rHWLock.eDriverType);
				gLockTimeOutCount = 0;
				mutex_unlock(&VencHWLock);
				return -EFAULT;
			}
		}
		mutex_unlock(&VencHWLock);
	}

	return 0;
}

void vcodec_venc_pmqos(struct codec_job *enc_cur_job)
{
#ifdef VCODEC_DVFS_V2
	int target_freq;
	u64 target_freq_64;

	mutex_lock(&VencDVFSLock);
	if (enc_cur_job == 0) {
		target_freq_64 = match_freq(99999,
			&g_enc_freq_steps[0], enc_step_size);
		pm_qos_update_request(&vcodec_qos_request_f2,
			target_freq_64);
	} else {
		enc_cur_job->start = get_time_us();
		target_freq = est_freq(enc_cur_job->handle,
			&enc_jobs, enc_hists);
		target_freq_64 = match_freq(target_freq,
			&g_enc_freq_steps[0], enc_step_size);
		if (target_freq > 0) {
			enc_cur_job->mhz = (int)target_freq_64;
			pm_qos_update_request(
				&vcodec_qos_request_f2,
			target_freq_64);
		}
	}
	DVFS_DEBUG("enc_cur_job freq %llu", target_freq_64);
	mutex_unlock(&VencDVFSLock);
#endif
}

static long vcodec_lockhw(unsigned long arg)
{
	VAL_UINT8_T *user_data_addr;
	VAL_HW_LOCK_T rHWLock;
	VAL_RESULT_T eValRet;
	VAL_LONG_T ret;
	VAL_BOOL_T bLockedHW = VAL_FALSE;
	VAL_UINT32_T FirstUseDecHW = 0;
	VAL_UINT32_T FirstUseEncHW = 0;
	VAL_TIME_T rCurTime;
	VAL_UINT32_T u4TimeInterval;
	VAL_ULONG_T ulFlagsLockHW;
	VAL_ULONG_T handle_id = 0;
#if USE_WAKELOCK == 0
	unsigned int suspend_block_cnt = 0;
#endif
#ifdef VCODEC_DVFS_V2
	struct codec_job *dec_cur_job = 0;
	struct codec_job *enc_cur_job = 0;

	int target_freq;
	u64 target_freq_64;
#endif

	MODULE_MFV_LOGD("VCODEC_LOCKHW + tid = %d\n", current->pid);

	user_data_addr = (VAL_UINT8_T *)arg;
	ret = copy_from_user(&rHWLock, user_data_addr, sizeof(VAL_HW_LOCK_T));
	if (ret) {
		pr_debug("[ERROR] VCODEC_LOCKHW, copy_from_user failed: %lu\n", ret);
		return -EFAULT;
	}

	MODULE_MFV_LOGD("[VCODEC] LOCKHW eDriverType = %d\n", rHWLock.eDriverType);
	eValRet = VAL_RESULT_INVALID_ISR;
	if (rHWLock.eDriverType == VAL_DRIVER_TYPE_MP4_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_HEVC_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_H264_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_MP1_MP2_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_VC1_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_VC1_ADV_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_VP8_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_VP9_DEC) {

#ifdef VCODEC_DVFS_V2
		mutex_lock(&VdecDVFSLock);
		handle_id = pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle);
		if (handle_id == 0) {
			DVFS_DEBUG("[error] handle is freed at %d\n", __LINE__);
			mutex_unlock(&VdecDVFSLock);
			return -1;
		}
		dec_cur_job = add_job((void *)handle_id, &dec_jobs);
		DVFS_DEBUG("dec_cur_job's handle %p", dec_cur_job->handle);
		mutex_unlock(&VdecDVFSLock);
#endif

		while (bLockedHW == VAL_FALSE) {
			mutex_lock(&DecHWLockEventTimeoutLock);
			if (DecHWLockEvent.u4TimeoutMs == 1) {
				pr_debug("VCODEC_LOCKHW, First Use Dec HW!!\n");
				FirstUseDecHW = 1;
			} else {
				FirstUseDecHW = 0;
			}
			mutex_unlock(&DecHWLockEventTimeoutLock);

			if (FirstUseDecHW == 1) {
				/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
				eValRet = eVideoWaitEvent(&DecHWLockEvent, sizeof(VAL_EVENT_T));
			}
			mutex_lock(&DecHWLockEventTimeoutLock);
			if (DecHWLockEvent.u4TimeoutMs != 1000) {
				DecHWLockEvent.u4TimeoutMs = 1000;
				FirstUseDecHW = 1;
			} else {
				FirstUseDecHW = 0;
			}
			mutex_unlock(&DecHWLockEventTimeoutLock);

			mutex_lock(&VdecHWLock);
			handle_id = pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle);
			if (handle_id == 0) {
				pr_debug("[error] handle is freed at %d\n", __LINE__);
				mutex_unlock(&VdecHWLock);
				return -1;
			}
			/* one process try to lock twice */
			if (grVcodecDecHWLock.pvHandle ==
				(VAL_VOID_T *)handle_id) {
				pr_debug("[WARNING] VCODEC_LOCKHW, one decoder instance try to lock twice\n");
				pr_debug("may cause lock HW timeout!! instance = 0x%lx, CurrentTID = %d\n",
				(VAL_ULONG_T)grVcodecDecHWLock.pvHandle, current->pid);
			}
			mutex_unlock(&VdecHWLock);

			if (FirstUseDecHW == 0) {
				MODULE_MFV_LOGD("VCODEC_LOCKHW, Not first time use HW, timeout = %d\n",
					 DecHWLockEvent.u4TimeoutMs);
				eValRet = eVideoWaitEvent(&DecHWLockEvent, sizeof(VAL_EVENT_T));
			}

			if (eValRet == VAL_RESULT_INVALID_ISR) {
				ret = vcodec_lockhw_dec_fail(rHWLock, FirstUseDecHW);
				if (ret) {
					pr_debug("[ERROR] vcodec_lockhw_dec_fail failed: %lu\n", ret);
					return -EFAULT;
				}
			} else if (eValRet == VAL_RESULT_RESTARTSYS) {
				pr_debug("[WARNING] VCODEC_LOCKHW, VAL_RESULT_RESTARTSYS return when HWLock!!\n");
				return -ERESTARTSYS;
			}

			mutex_lock(&VdecHWLock);
			if (grVcodecDecHWLock.pvHandle == 0) { /* No one holds dec hw lock now */
#if USE_WAKELOCK == 1
				MODULE_MFV_PR_DEBUG("wake_lock(&vcodec_wake_lock) +");
				wake_lock(&vcodec_wake_lock);
				MODULE_MFV_PR_DEBUG("wake_lock(&vcodec_wake_lock) -");
#elif USE_WAKELOCK == 0
				while (is_entering_suspend == 1) {
					suspend_block_cnt++;
					if (suspend_block_cnt > 100000) {
						/* Print log if trying to enter suspend for too long */
						MODULE_MFV_PR_DEBUG("VCODEC_LOCKHW blocked by suspend flow for long time");
						suspend_block_cnt = 0;
					}
					msleep(1);
				}
#endif
				gu4VdecLockThreadId = current->pid;
				handle_id = pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle);
				if (handle_id == 0) {
					pr_debug("[error] handle is freed at %d\n", __LINE__);
					mutex_unlock(&VdecHWLock);
					return -1;
				}
				grVcodecDecHWLock.pvHandle =
					(VAL_VOID_T *)handle_id;
				grVcodecDecHWLock.eDriverType = rHWLock.eDriverType;
				eVideoGetTimeOfDay(&grVcodecDecHWLock.rLockedTime, sizeof(VAL_TIME_T));

				MODULE_MFV_LOGD("VCODEC_LOCKHW, No process use dec HW, so current process can use HW\n");
				MODULE_MFV_LOGD("LockInstance = 0x%lx CurrentTID = %d, rLockedTime(s, us) = %d, %d\n",
					 (VAL_ULONG_T)grVcodecDecHWLock.pvHandle,
					 current->pid,
					 grVcodecDecHWLock.rLockedTime.u4Sec, grVcodecDecHWLock.rLockedTime.u4uSec);

				bLockedHW = VAL_TRUE;
#ifdef VCODEC_DVFS_V2
				mutex_lock(&VdecDVFSLock);
				if (dec_cur_job == 0) {
					target_freq_64 = match_freq(99999,
						&g_dec_freq_steps[0], dec_step_size);
					pm_qos_update_request(&vcodec_qos_request_f,
						target_freq_64);
				} else {
					dec_cur_job->start = get_time_us();
					target_freq = est_freq(dec_cur_job->handle,
							&dec_jobs, dec_hists);
					target_freq_64 = match_freq(target_freq,
						&g_dec_freq_steps[0], dec_step_size);
					if (target_freq > 0) {
						dec_cur_job->mhz = (int)target_freq_64;
						pm_qos_update_request(
							&vcodec_qos_request_f,
							target_freq_64);
					}
				}
				DVFS_DEBUG("dec_cur_job freq %llu", target_freq_64);
				mutex_unlock(&VdecDVFSLock);
#endif
#ifdef CONFIG_PM
				pm_runtime_get_sync(vcodec_device);
#else
#ifndef KS_POWER_WORKAROUND
				vdec_power_on();
#endif
#endif
#ifdef CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT	/* Morris Yang moved to TEE */
				if (rHWLock.bSecureInst == VAL_FALSE) {
					if (request_irq
						(VDEC_IRQ_ID,
						(irq_handler_t) video_intr_dlr,
						IRQF_TRIGGER_LOW, VCODEC_DEVNAME,
						NULL) < 0)
						MODULE_MFV_LOGD
							("[VCODEC_DEBUG][ERROR] error to request dec irq\n");
					else
						MODULE_MFV_LOGD
							("[VCODEC_DEBUG] success to request dec irq\n");
					/* enable_irq(VDEC_IRQ_ID); */
				}
#else
				enable_irq(VDEC_IRQ_ID);
#endif

#ifdef ENABLE_MMDVFS_VDEC
				VdecDvfsMonitorStart();
#endif


			} else { /* Another one holding dec hw now */
				pr_debug("VCODEC_LOCKHW E\n");
				eVideoGetTimeOfDay(&rCurTime, sizeof(VAL_TIME_T));
				u4TimeInterval = (((((rCurTime.u4Sec - grVcodecDecHWLock.rLockedTime.u4Sec) * 1000000)
						    + rCurTime.u4uSec) - grVcodecDecHWLock.rLockedTime.u4uSec) / 1000);

				MODULE_MFV_LOGD("VCODEC_LOCKHW, someone use dec HW, and check timeout value\n");
				MODULE_MFV_LOGD("TimeInterval(ms) = %d, TimeOutValue(ms)) = %d\n",
					 u4TimeInterval, rHWLock.u4TimeoutMs);
				MODULE_MFV_LOGD("Lock Instance = 0x%lx, Lock TID = %d, CurrentTID = %d\n",
					 (VAL_ULONG_T)grVcodecDecHWLock.pvHandle,
					 gu4VdecLockThreadId,
					 current->pid);
				MODULE_MFV_LOGD("rLockedTime(%d s, %d us), rCurTime(%d s, %d us)\n",
					grVcodecDecHWLock.rLockedTime.u4Sec, grVcodecDecHWLock.rLockedTime.u4uSec,
					rCurTime.u4Sec, rCurTime.u4uSec);

				/* 2012/12/16. Cheng-Jung Never steal hardware lock */
			}
			mutex_unlock(&VdecHWLock);
			spin_lock_irqsave(&LockDecHWCountLock, ulFlagsLockHW);
			gu4LockDecHWCount++;
			spin_unlock_irqrestore(&LockDecHWCountLock, ulFlagsLockHW);
		}
	} else if (rHWLock.eDriverType == VAL_DRIVER_TYPE_H264_ENC ||
		   rHWLock.eDriverType == VAL_DRIVER_TYPE_HEVC_ENC ||
		   rHWLock.eDriverType == VAL_DRIVER_TYPE_JPEG_ENC) {
		while (bLockedHW == VAL_FALSE) {
			/* Early break for JPEG VENC */
			if (rHWLock.u4TimeoutMs == 0) {
				if (grVcodecEncHWLock.pvHandle != 0) {
					/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
					break;
				}
			}

			/* Wait to acquire Enc HW lock */
			mutex_lock(&EncHWLockEventTimeoutLock);
			if (EncHWLockEvent.u4TimeoutMs == 1) {
				pr_debug("VCODEC_LOCKHW, First Use Enc HW %d!!\n", rHWLock.eDriverType);
				FirstUseEncHW = 1;
			} else {
				FirstUseEncHW = 0;
			}
			mutex_unlock(&EncHWLockEventTimeoutLock);
			if (FirstUseEncHW == 1) {
				/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
				eValRet = eVideoWaitEvent(&EncHWLockEvent, sizeof(VAL_EVENT_T));
			}

			mutex_lock(&EncHWLockEventTimeoutLock);
			if (EncHWLockEvent.u4TimeoutMs == 1) {
				EncHWLockEvent.u4TimeoutMs = 1000;
				FirstUseEncHW = 1;
			} else {
				FirstUseEncHW = 0;
				if (rHWLock.u4TimeoutMs == 0) {
					/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
					EncHWLockEvent.u4TimeoutMs = 0; /* No wait */
				} else {
					EncHWLockEvent.u4TimeoutMs = 1000; /* Wait indefinitely */
				}
			}
			mutex_unlock(&EncHWLockEventTimeoutLock);

			mutex_lock(&VencHWLock);
			handle_id = pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle);
			if (handle_id == 0) {
				pr_debug("[error] handle is freed at %d\n", __LINE__);
				mutex_unlock(&VencHWLock);
				return -1;
			}
			/* one process try to lock twice */
			if (grVcodecEncHWLock.pvHandle ==
			    (VAL_VOID_T *)handle_id) {
				pr_debug("[WARNING] VCODEC_LOCKHW, one encoder instance try to lock twice\n");
				pr_debug("may cause lock HW timeout!! instance=0x%lx, CurrentTID=%d, type:%d\n",
					(VAL_ULONG_T)grVcodecEncHWLock.pvHandle, current->pid, rHWLock.eDriverType);
			}
			mutex_unlock(&VencHWLock);

			if (FirstUseEncHW == 0) {
				/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
				eValRet = eVideoWaitEvent(&EncHWLockEvent, sizeof(VAL_EVENT_T));
			}

			if (eValRet == VAL_RESULT_INVALID_ISR) {
				ret = vcodec_lockhw_enc_fail(rHWLock, FirstUseEncHW);
				if (ret) {
					pr_debug("[ERROR] vcodec_lockhw_enc_fail failed: %lu\n", ret);
					return -EFAULT;
				}
			} else if (eValRet == VAL_RESULT_RESTARTSYS) {
				return -ERESTARTSYS;
			}

			mutex_lock(&VencHWLock);
			if (grVcodecEncHWLock.pvHandle == 0) { /* No process use HW, so current process can use HW */
				if (rHWLock.eDriverType == VAL_DRIVER_TYPE_H264_ENC ||
					rHWLock.eDriverType == VAL_DRIVER_TYPE_HEVC_ENC ||
					rHWLock.eDriverType == VAL_DRIVER_TYPE_JPEG_ENC) {
#ifdef VCODEC_DVFS_V2
					mutex_lock(&VencDVFSLock);
					handle_id = pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle);
					if (handle_id == 0) {
						DVFS_DEBUG("[error] handle is freed at %d\n",
						__LINE__);
						mutex_unlock(&VencDVFSLock);
						mutex_unlock(&VencHWLock);
						return -1;
					}
					enc_cur_job =
					  add_job(
						(void *)handle_id,
						&enc_jobs);
					DVFS_DEBUG("enc_cur_job's handle %p", enc_cur_job->handle);
					mutex_unlock(&VencDVFSLock);
#endif
#if USE_WAKELOCK == 1
					MODULE_MFV_PR_DEBUG("wake_lock(&vcodec_wake_lock2) +");
					wake_lock(&vcodec_wake_lock2);
					MODULE_MFV_PR_DEBUG("wake_lock(&vcodec_wake_lock2) -");
#elif USE_WAKELOCK == 0
					while (is_entering_suspend == 1) {
					suspend_block_cnt++;
					if (suspend_block_cnt > 100000) {
						/* Print log if trying to enter suspend for too long */
						MODULE_MFV_PR_DEBUG("VCODEC_LOCKHW blocked by suspend flow for long time");
						suspend_block_cnt = 0;
					}
					msleep(1);
					}
#endif
					handle_id = pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle);
					if (handle_id == 0) {
						pr_debug("[error] handle is freed at %d\n",
						__LINE__);
						mutex_unlock(&VencHWLock);
						return -1;
					}
					grVcodecEncHWLock.pvHandle =
						(VAL_VOID_T *)handle_id;
					grVcodecEncHWLock.eDriverType = rHWLock.eDriverType;
					eVideoGetTimeOfDay(&grVcodecEncHWLock.rLockedTime, sizeof(VAL_TIME_T));

					MODULE_MFV_LOGD("VCODEC_LOCKHW, No process use HW, so current process can use HW\n");
					MODULE_MFV_LOGD("VCODEC_LOCKHW, handle = 0x%lx\n",
						 (VAL_ULONG_T)grVcodecEncHWLock.pvHandle);
					MODULE_MFV_LOGD("LockInstance = 0x%lx CurrentTID = %d, rLockedTime(s, us) = %d, %d\n",
						 (VAL_ULONG_T)grVcodecEncHWLock.pvHandle,
						 current->pid,
						 grVcodecEncHWLock.rLockedTime.u4Sec,
						 grVcodecEncHWLock.rLockedTime.u4uSec);

					bLockedHW = VAL_TRUE;
					if (rHWLock.eDriverType == VAL_DRIVER_TYPE_H264_ENC ||
						rHWLock.eDriverType == VAL_DRIVER_TYPE_HEVC_ENC) {
						vcodec_venc_pmqos(enc_cur_job);
						venc_enableIRQ(&rHWLock);
					}
				}
			} else { /* someone use HW, and check timeout value */
				if (rHWLock.u4TimeoutMs == 0) {
					bLockedHW = VAL_FALSE;
					mutex_unlock(&VencHWLock);
					break;
				}

				eVideoGetTimeOfDay(&rCurTime, sizeof(VAL_TIME_T));
				u4TimeInterval = (((((rCurTime.u4Sec - grVcodecEncHWLock.rLockedTime.u4Sec) * 1000000)
					+ rCurTime.u4uSec) - grVcodecEncHWLock.rLockedTime.u4uSec) / 1000);

				MODULE_MFV_LOGD("VCODEC_LOCKHW, someone use enc HW, and check timeout value\n");
				MODULE_MFV_LOGD("TimeInterval(ms) = %d, TimeOutValue(ms) = %d\n",
					 u4TimeInterval, rHWLock.u4TimeoutMs);
				MODULE_MFV_LOGD("rLockedTime(s, us) = %d, %d, rCurTime(s, us) = %d, %d\n",
					 grVcodecEncHWLock.rLockedTime.u4Sec, grVcodecEncHWLock.rLockedTime.u4uSec,
					 rCurTime.u4Sec, rCurTime.u4uSec);
				MODULE_MFV_LOGD("LockInstance = 0x%lx, CurrentInstance = 0x%lx, CurrentTID = %d\n",
					 (VAL_ULONG_T)grVcodecEncHWLock.pvHandle,
					 pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle),
					 current->pid);

				++gLockTimeOutCount;
				if (gLockTimeOutCount > 30) {
					pr_debug("[ERROR] VCODEC_LOCKHW %d fail,someone locked HW over 30 times\n",
						 current->pid);
					pr_debug("without timeout 0x%lx,%lx,0x%lx,type:%d\n",
						 (VAL_ULONG_T)grVcodecEncHWLock.pvHandle,
						 pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle),
						 (VAL_ULONG_T)rHWLock.pvHandle,
						 rHWLock.eDriverType);
					gLockTimeOutCount = 0;
					mutex_unlock(&VencHWLock);
					return -EFAULT;
				}

				/* 2013/04/10. Cheng-Jung Never steal hardware lock */
			}

			if (bLockedHW == VAL_TRUE) {
				MODULE_MFV_LOGD("VCODEC_LOCKHW, Lock ok grVcodecEncHWLock.pvHandle = 0x%lx, va:%lx, type:%d\n",
					 (VAL_ULONG_T)grVcodecEncHWLock.pvHandle,
					 (VAL_ULONG_T)rHWLock.pvHandle,
					 rHWLock.eDriverType);
				gLockTimeOutCount = 0;
			}
			mutex_unlock(&VencHWLock);
		}

		if (bLockedHW == VAL_FALSE) {
			pr_debug("[ERROR] VCODEC_LOCKHW %d fail,someone locked HW already,0x%lx,%lx,0x%lx,type:%d\n",
				 current->pid,
				 (VAL_ULONG_T)grVcodecEncHWLock.pvHandle,
				 pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle),
				 (VAL_ULONG_T)rHWLock.pvHandle,
				 rHWLock.eDriverType);
			gLockTimeOutCount = 0;
			return -EFAULT;
		}

		spin_lock_irqsave(&LockEncHWCountLock, ulFlagsLockHW);
		gu4LockEncHWCount++;
		spin_unlock_irqrestore(&LockEncHWCountLock, ulFlagsLockHW);

		MODULE_MFV_LOGD("VCODEC_LOCKHW, get locked - ObjId =%d\n", current->pid);

		MODULE_MFV_LOGD("VCODEC_LOCKHWed - tid = %d\n", current->pid);
	} else {
		pr_debug("[WARNING] VCODEC_LOCKHW Unknown instance\n");
		return -EFAULT;
	}

	MODULE_MFV_LOGD("VCODEC_LOCKHW - tid = %d\n", current->pid);

	return 0;
}

static long vcodec_unlockhw(unsigned long arg)
{
	VAL_UINT8_T *user_data_addr;
	VAL_HW_LOCK_T rHWLock;
	VAL_RESULT_T eValRet;
	VAL_LONG_T ret;
	VAL_ULONG_T handle_id = 0;
#ifdef VCODEC_DVFS_V2
	struct codec_job *dec_cur_job;
	struct codec_job *enc_cur_job;
#endif

	MODULE_MFV_LOGD("VCODEC_UNLOCKHW + tid = %d\n", current->pid);

	user_data_addr = (VAL_UINT8_T *)arg;
	ret = copy_from_user(&rHWLock, user_data_addr, sizeof(VAL_HW_LOCK_T));
	if (ret) {
		pr_debug("[ERROR] VCODEC_UNLOCKHW, copy_from_user failed: %lu\n", ret);
		return -EFAULT;
	}

	MODULE_MFV_LOGD("VCODEC_UNLOCKHW eDriverType = %d\n", rHWLock.eDriverType);
	eValRet = VAL_RESULT_INVALID_ISR;
	if (rHWLock.eDriverType == VAL_DRIVER_TYPE_MP4_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_HEVC_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_H264_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_MP1_MP2_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_VC1_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_VC1_ADV_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_VP8_DEC ||
		rHWLock.eDriverType == VAL_DRIVER_TYPE_VP9_DEC) {
		mutex_lock(&VdecHWLock);
		handle_id = pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle);
		if (handle_id == 0) {
			pr_debug("[error] handle is freed at %d\n", __LINE__);
			mutex_unlock(&VdecHWLock);
			return -1;
		}
		/* Current owner give up hw lock */
		if (grVcodecDecHWLock.pvHandle == (VAL_VOID_T *)handle_id) {
#ifdef VCODEC_DVFS_V2
			mutex_lock(&VdecDVFSLock);
			dec_cur_job = dec_jobs;
			if (dec_cur_job->handle ==
				grVcodecDecHWLock.pvHandle) {
				dec_cur_job->end = get_time_us();
				update_hist(dec_cur_job, &dec_hists);
				dec_jobs = dec_jobs->next;
				kfree(dec_cur_job);
			} else {
				pr_info("VCODEC wrong job at dec done %p, %p",
					dec_cur_job->handle,
					grVcodecDecHWLock.pvHandle);
			}
			mutex_unlock(&VdecDVFSLock);
#endif
#ifdef CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT	/* Morris Yang moved to TEE */
			if (rHWLock.bSecureInst == VAL_FALSE) {
				/* disable_irq(VDEC_IRQ_ID); */

				free_irq(VDEC_IRQ_ID, NULL);
			}
#else
			disable_irq(VDEC_IRQ_ID);
#endif
			/* TODO: check if turning power off is ok */
#ifdef CONFIG_PM
			pm_runtime_put_sync(vcodec_device);
#else
#ifndef KS_POWER_WORKAROUND
			vdec_power_off();
#endif
#endif

#ifdef ENABLE_MMDVFS_VDEC
			VdecDvfsAdjustment();
#endif
			grVcodecDecHWLock.pvHandle = 0;
			grVcodecDecHWLock.eDriverType = VAL_DRIVER_TYPE_NONE;
		} else { /* Not current owner */
			pr_debug("[ERROR] VCODEC_UNLOCKHW\n");
			pr_debug("Not owner trying to unlock dec hardware 0x%lx\n",
				 pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle));
			mutex_unlock(&VdecHWLock);
			return -EFAULT;
		}
#if USE_WAKELOCK == 1
		MODULE_MFV_PR_DEBUG("wake_unlock(&vcodec_wake_lock) +");
		wake_unlock(&vcodec_wake_lock);
		MODULE_MFV_PR_DEBUG("wake_unlock(&vcodec_wake_lock) -");
#endif
		mutex_unlock(&VdecHWLock);
		eValRet = eVideoSetEvent(&DecHWLockEvent, sizeof(VAL_EVENT_T));
	} else if (rHWLock.eDriverType == VAL_DRIVER_TYPE_H264_ENC ||
			 rHWLock.eDriverType == VAL_DRIVER_TYPE_HEVC_ENC ||
			 rHWLock.eDriverType == VAL_DRIVER_TYPE_JPEG_ENC) {
		mutex_lock(&VencHWLock);
		handle_id = pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle);
		if (handle_id == 0) {
			pr_debug("[error] handle is freed at %d\n", __LINE__);
			mutex_unlock(&VencHWLock);
			return -1;
		}
		/* Current owner give up hw lock */
		if (grVcodecEncHWLock.pvHandle == (VAL_VOID_T *)handle_id) {
#ifdef VCODEC_DVFS_V2
			mutex_lock(&VencDVFSLock);
			enc_cur_job = enc_jobs;
			if (enc_cur_job->handle ==
				grVcodecEncHWLock.pvHandle) {
				enc_cur_job->end = get_time_us();
				update_hist(enc_cur_job, &enc_hists);
				enc_jobs = enc_jobs->next;
				kfree(enc_cur_job);
			} else {
				pr_info("VCODEC wrong job at dec done %p, %p",
					enc_cur_job->handle,
					grVcodecEncHWLock.pvHandle);
			}
			mutex_unlock(&VencDVFSLock);
#endif
			if (rHWLock.eDriverType == VAL_DRIVER_TYPE_H264_ENC ||
				rHWLock.eDriverType == VAL_DRIVER_TYPE_HEVC_ENC) {
				venc_disableIRQ(&rHWLock);
			}
			grVcodecEncHWLock.pvHandle = 0;
			grVcodecEncHWLock.eDriverType = VAL_DRIVER_TYPE_NONE;
		} else { /* Not current owner */
			/* [TODO] error handling */
			pr_debug("[ERROR] VCODEC_UNLOCKHW\n");
			pr_debug("Not owner trying to unlock enc hardware 0x%lx, pa:%lx, va:%lx type:%d\n",
				 (VAL_ULONG_T)grVcodecEncHWLock.pvHandle,
				 pmem_user_v2p_video((VAL_ULONG_T)rHWLock.pvHandle),
				 (VAL_ULONG_T)rHWLock.pvHandle,
				 rHWLock.eDriverType);
			mutex_unlock(&VencHWLock);
			return -EFAULT;
			}
#if USE_WAKELOCK == 1
		MODULE_MFV_PR_DEBUG("wake_unlock(&vcodec_wake_lock2) +");
		wake_unlock(&vcodec_wake_lock2);
		MODULE_MFV_PR_DEBUG("wake_unlock(&vcodec_wake_lock2) -");
#endif
		mutex_unlock(&VencHWLock);
		eValRet = eVideoSetEvent(&EncHWLockEvent, sizeof(VAL_EVENT_T));
	} else {
		pr_debug("[WARNING] VCODEC_UNLOCKHW Unknown instance\n");
		return -EFAULT;
	}

	MODULE_MFV_LOGD("VCODEC_UNLOCKHW - tid = %d\n", current->pid);

	return 0;
}

static long vcodec_waitisr(unsigned long arg)
{
	VAL_UINT8_T *user_data_addr;
	VAL_ISR_T val_isr;
	VAL_BOOL_T bLockedHW = VAL_FALSE;
	VAL_ULONG_T ulFlags;
	VAL_LONG_T ret;
	VAL_RESULT_T eValRet;
	VAL_ULONG_T handle_id = 0;

	MODULE_MFV_LOGD("VCODEC_WAITISR + tid = %d\n", current->pid);

	user_data_addr = (VAL_UINT8_T *)arg;
	ret = copy_from_user(&val_isr, user_data_addr, sizeof(VAL_ISR_T));
	if (ret) {
		pr_debug("[ERROR] VCODEC_WAITISR, copy_from_user failed: %lu\n", ret);
		return -EFAULT;
	}

	if (val_isr.eDriverType == VAL_DRIVER_TYPE_MP4_DEC ||
		val_isr.eDriverType == VAL_DRIVER_TYPE_HEVC_DEC ||
		val_isr.eDriverType == VAL_DRIVER_TYPE_H264_DEC ||
		val_isr.eDriverType == VAL_DRIVER_TYPE_MP1_MP2_DEC ||
		val_isr.eDriverType == VAL_DRIVER_TYPE_VC1_DEC ||
		val_isr.eDriverType == VAL_DRIVER_TYPE_VC1_ADV_DEC ||
		val_isr.eDriverType == VAL_DRIVER_TYPE_VP8_DEC ||
		val_isr.eDriverType == VAL_DRIVER_TYPE_VP9_DEC) {
		mutex_lock(&VdecHWLock);
		handle_id = pmem_user_v2p_video((VAL_ULONG_T)val_isr.pvHandle);
		if (handle_id == 0) {
			pr_debug("[error] handle is freed at %d\n", __LINE__);
			mutex_unlock(&VdecHWLock);
			return -1;
		}
		if (grVcodecDecHWLock.pvHandle == (VAL_VOID_T *)handle_id) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			bLockedHW = VAL_TRUE;
		} else {
		}
		mutex_unlock(&VdecHWLock);

		if (bLockedHW == VAL_FALSE) {
			pr_debug("[ERROR] VCODEC_WAITISR, DO NOT have HWLock, so return fail\n");
			return -EFAULT;
		}

		spin_lock_irqsave(&DecIsrLock, ulFlags);
		DecIsrEvent.u4TimeoutMs = val_isr.u4TimeoutMs;
		spin_unlock_irqrestore(&DecIsrLock, ulFlags);

		eValRet = eVideoWaitEvent(&DecIsrEvent, sizeof(VAL_EVENT_T));
		if (eValRet == VAL_RESULT_INVALID_ISR) {
			return -2;
		} else if (eValRet == VAL_RESULT_RESTARTSYS) {
			pr_debug("[WARNING] VCODEC_WAITISR, VAL_RESULT_RESTARTSYS return when WAITISR!!\n");
			return -ERESTARTSYS;
		}
	} else if (val_isr.eDriverType == VAL_DRIVER_TYPE_H264_ENC ||
		   val_isr.eDriverType == VAL_DRIVER_TYPE_HEVC_ENC) {
		mutex_lock(&VencHWLock);
		handle_id = pmem_user_v2p_video((VAL_ULONG_T)val_isr.pvHandle);
		if (handle_id == 0) {
			pr_debug("[error] handle is freed at %d\n", __LINE__);
			mutex_unlock(&VencHWLock);
			return -1;
		}
		if (grVcodecEncHWLock.pvHandle == (VAL_VOID_T *)handle_id) {
			/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
			bLockedHW = VAL_TRUE;
		} else {
		}
		mutex_unlock(&VencHWLock);

		if (bLockedHW == VAL_FALSE) {
			pr_debug("[ERROR] VCODEC_WAITISR, DO NOT have enc HWLock, so return fail pa:%lx, va:%lx\n",
				 pmem_user_v2p_video((VAL_ULONG_T)val_isr.pvHandle),
				 (VAL_ULONG_T)val_isr.pvHandle);
			return -EFAULT;
		}

		spin_lock_irqsave(&EncIsrLock, ulFlags);
		EncIsrEvent.u4TimeoutMs = val_isr.u4TimeoutMs;
		spin_unlock_irqrestore(&EncIsrLock, ulFlags);

		eValRet = eVideoWaitEvent(&EncIsrEvent, sizeof(VAL_EVENT_T));
		if (eValRet == VAL_RESULT_INVALID_ISR) {
			return -2;
		} else if (eValRet == VAL_RESULT_RESTARTSYS) {
			pr_debug("[WARNING] VCODEC_WAITISR, VAL_RESULT_RESTARTSYS return when WAITISR!!\n");
			return -ERESTARTSYS;
		}

		if (val_isr.u4IrqStatusNum > 0) {
			val_isr.u4IrqStatus[0] = gu4HwVencIrqStatus;
			ret = copy_to_user(user_data_addr, &val_isr, sizeof(VAL_ISR_T));
			if (ret) {
				pr_debug("[ERROR] VCODEC_WAITISR, copy_to_user failed: %lu\n", ret);
				return -EFAULT;
			}
		}
	} else {
		pr_debug("[WARNING] VCODEC_WAITISR Unknown instance\n");
		return -EFAULT;
	}

	MODULE_MFV_LOGD("VCODEC_WAITISR - tid = %d\n", current->pid);

	return 0;
}

static long vcodec_set_frame_info(unsigned long arg)
{
	VAL_UINT8_T *user_data_addr;
	VAL_LONG_T ret;
	struct VAL_FRAME_INFO_T rFrameInfo;
	VAL_INT32_T frame_type = 0;
	VAL_INT32_T b_freq_idx = 0;
	long emi_bw = 0;

	MODULE_MFV_LOGD("VCODEC_SET_FRAME_INFO + tid = %d\n", current->pid);
	user_data_addr = (VAL_UINT8_T *)arg;
	ret = copy_from_user(&rFrameInfo, user_data_addr, sizeof(struct VAL_FRAME_INFO_T));
	if (ret) {
		MODULE_MFV_LOGD("[ERROR] VCODEC_SET_FRAME_INFO, copy_from_user failed: %lu\n", ret);
		mutex_unlock(&LogCountLock);
		return -EFAULT;
	}

/* TODO check user
 *	mutex_lock(&VDecHWLock);
 *	if (grVcodecDecHWLock.pvHandle !=
 *				(VAL_VOID_T *)pmem_user_v2p_video((VAL_ULONG_T)rFrameInfo.handle)) {
 *
 *		MODULE_MFV_LOGD("[ERROR] VCODEC_SET_FRAME_INFO, does not have vdec resource");
 *		mutex_unlock(&VDecHWLock);
 *		return -EFAULT;
 *	}
 *	mutex_unlock(&VDecHWLock);
 */
	if (rFrameInfo.driver_type == VAL_DRIVER_TYPE_H264_DEC ||
		rFrameInfo.driver_type == VAL_DRIVER_TYPE_HEVC_DEC ||
		rFrameInfo.driver_type == VAL_DRIVER_TYPE_MP4_DEC ||
		rFrameInfo.driver_type == VAL_DRIVER_TYPE_MP1_MP2_DEC) {
		mutex_lock(&DecPMQoSLock);
		/* Request BW after lock hw, this should always be true */
		if (gVDECBWRequested == 0) {
			frame_type = rFrameInfo.frame_type;
			if (frame_type > 3 || frame_type < 0)
				frame_type = 0;

			if (dec_step_size > 1)
				b_freq_idx = dec_step_size - 1;

			/* 8bit * w * h * 1.5 * frame type ratio * freq ratio * decoding time relative to 1080p */
			emi_bw = 8L * 3 * g_dec_freq_steps[gVDECLevel] * 100 * 1920 * 1088;
			switch (rFrameInfo.driver_type) {
			case VAL_DRIVER_TYPE_H264_DEC:
				emi_bw = emi_bw * gVDECFrmTRAVC[frame_type] /
					(2 * 3 * gVDECFreq[b_freq_idx]);
				QOS_DEBUG("[PMQoS Kernel] AVC frame_type %d, ratio %d, VDec freq %d emi_bw %ld",
				frame_type, gVDECFrmTRAVC[frame_type], gVDECFreq[gVDECLevel], emi_bw);
				break;
			case VAL_DRIVER_TYPE_HEVC_DEC:
				emi_bw = emi_bw * gVDECFrmTRHEVC[frame_type] /
					(2 * 3 * gVDECFreq[b_freq_idx]);
				QOS_DEBUG("[PMQoS Kernel] HEVC frame_type %d, ratio %d, VDec freq %d emi_bw %ld",
				frame_type, gVDECFrmTRHEVC[frame_type], gVDECFreq[gVDECLevel], emi_bw);
				break;
			case VAL_DRIVER_TYPE_MP4_DEC:
			case VAL_DRIVER_TYPE_MP1_MP2_DEC:
				emi_bw = emi_bw * gVDECFrmTRMP2_4[frame_type] /
					(2 * 3 * gVDECFreq[b_freq_idx]);
				QOS_DEBUG("[PMQoS Kernel] MP2_4 frame_type %d, ratio %d, VDec freq %d emi_bw %ld",
				frame_type, gVDECFrmTRMP2_4[frame_type], gVDECFreq[gVDECLevel], emi_bw);
				break;
			default:
				QOS_DEBUG("[PMQoS Kernel] Unsupported decoder type");
			}

			if (rFrameInfo.is_compressed != 0)
				emi_bw = emi_bw * 6 / 10;
			QOS_DEBUG("[PMQoS Kernel] UFO %d, emi_bw %ld", rFrameInfo.is_compressed, emi_bw);

			/* input size */
			emi_bw += 8 * rFrameInfo.input_size * 100 * 1920 * 1088
				/ (rFrameInfo.frame_width * rFrameInfo.frame_height);

			QOS_DEBUG("[PMQoS Kernel] input_size %d, width %d, height %d emi_bw %ld",
				rFrameInfo.input_size, rFrameInfo.frame_width, rFrameInfo.frame_height, emi_bw);

			emi_bw = emi_bw / (1024*1024) / 8; /* bits/s to mbytes/s */
			QOS_DEBUG("[PMQoS Kernel] mbytes/s emi_bw %ld", emi_bw);
#ifdef CONFIG_MTK_QOS_SUPPORT
			pm_qos_update_request(&vcodec_qos_request, (int)emi_bw);
#endif
			gVDECBWRequested = 1;
		}
		mutex_unlock(&DecPMQoSLock);
	} else if (rFrameInfo.driver_type == VAL_DRIVER_TYPE_H264_ENC) {
		mutex_lock(&EncPMQoSLock);
		{
		if (rFrameInfo.frame_width * rFrameInfo.frame_height < 1920*1088) {
			switch (rFrameInfo.frame_type) {
			case 1:
				emi_bw = 560; /* MB/s */
			break;
			case 0:
			default:
				emi_bw = 210; /* MB/s */
			break;
			}
		} else {
			switch (rFrameInfo.frame_type) {
			case 1:
				emi_bw = 1000; /* MB/s */
			break;
			case 0:
			default:
				emi_bw = 590; /* MB/s */
			break;
			}
		}
		QOS_DEBUG("[PMQoS Kernel] VENC mbytes/s emi_bw %ld", emi_bw);
#ifdef CONFIG_MTK_QOS_SUPPORT
		pm_qos_update_request(&vcodec_qos_request2, (int)emi_bw);
#endif
		gVENCBWRequested = 1;
		}
		mutex_unlock(&EncPMQoSLock);
	}
	MODULE_MFV_LOGD("VCODEC_SET_FRAME_INFO - tid = %d\n", current->pid);

	return 0;
}

static long vcodec_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	VAL_LONG_T ret;
	VAL_UINT8_T *user_data_addr;
	VAL_VCODEC_CORE_LOADING_T rTempCoreLoading;
	VAL_VCODEC_CPU_OPP_LIMIT_T rCpuOppLimit;
	VAL_INT32_T temp_nr_cpu_ids;
	VAL_POWER_T rPowerParam;
	VAL_BOOL_T rIncLogCount;

#if 0
	VCODEC_DRV_CMD_QUEUE_T rDrvCmdQueue;
	P_VCODEC_DRV_CMD_T cmd_queue = VAL_NULL;
	VAL_UINT32_T u4Size, uValue, nCount;
#endif

	switch (cmd) {
	case VCODEC_SET_THREAD_ID:
	{
		/* pr_debug("VCODEC_SET_THREAD_ID [EMPTY] + tid = %d\n", current->pid); */
		/* pr_debug("VCODEC_SET_THREAD_ID [EMPTY] - tid = %d\n", current->pid); */
	}
	break;

	case VCODEC_ALLOC_NON_CACHE_BUFFER:
	{
		/* MODULE_MFV_LOGE("VCODEC_ALLOC_NON_CACHE_BUFFER [EMPTY] + tid = %d\n", current->pid); */
	}
	break;

	case VCODEC_FREE_NON_CACHE_BUFFER:
	{
		/* MODULE_MFV_LOGE("VCODEC_FREE_NON_CACHE_BUFFER [EMPTY] + tid = %d\n", current->pid); */
	}
	break;

	case VCODEC_INC_DEC_EMI_USER:
	{
		MODULE_MFV_LOGD("VCODEC_INC_DEC_EMI_USER + tid = %d\n", current->pid);

		mutex_lock(&DecEMILock);
		gu4DecEMICounter++;
		MODULE_MFV_LOGD("[VCODEC] DEC_EMI_USER = %d\n", gu4DecEMICounter);
		user_data_addr = (VAL_UINT8_T *)arg;
		ret = copy_to_user(user_data_addr, &gu4DecEMICounter, sizeof(VAL_UINT32_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_INC_DEC_EMI_USER, copy_to_user failed: %lu\n", ret);
			mutex_unlock(&DecEMILock);
			return -EFAULT;
		}
		mutex_unlock(&DecEMILock);

#ifdef ENABLE_MMDVFS_VDEC
		/* MM DVFS related */
		/* pr_debug("[VCODEC][MMDVFS_VDEC] INC_DEC_EMI MM DVFS init\n"); */
		/* raise voltage */
		SendDvfsRequest(DVFS_DEFAULT);
		VdecDvfsBegin();
#endif

		MODULE_MFV_LOGD("VCODEC_INC_DEC_EMI_USER - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_DEC_DEC_EMI_USER:
	{
		MODULE_MFV_LOGD("VCODEC_DEC_DEC_EMI_USER + tid = %d\n", current->pid);

		mutex_lock(&DecEMILock);
		gu4DecEMICounter--;
		MODULE_MFV_LOGD("[VCODEC] DEC_EMI_USER = %d\n", gu4DecEMICounter);
		user_data_addr = (VAL_UINT8_T *)arg;
		ret = copy_to_user(user_data_addr, &gu4DecEMICounter, sizeof(VAL_UINT32_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_DEC_DEC_EMI_USER, copy_to_user failed: %lu\n", ret);
			mutex_unlock(&DecEMILock);
			return -EFAULT;
		}
#ifdef ENABLE_MMDVFS_VDEC
		/* MM DVFS related */
		/* MODULE_MFV_PR_DEBUG("[VCODEC][MMDVFS_VDEC] DEC_DEC_EMI MM DVFS\n"); */
		/* unrequest voltage */
		if (gu4DecEMICounter == 0) {
		/* Unrequest when all decoders exit */
			SendDvfsRequest(DVFS_UNREQUEST);
		}
#endif
		mutex_unlock(&DecEMILock);

		MODULE_MFV_LOGD("VCODEC_DEC_DEC_EMI_USER - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_INC_ENC_EMI_USER:
	{
		MODULE_MFV_LOGD("VCODEC_INC_ENC_EMI_USER + tid = %d\n", current->pid);

		mutex_lock(&EncEMILock);
		gu4EncEMICounter++;
		MODULE_MFV_LOGD("[VCODEC] ENC_EMI_USER = %d\n", gu4EncEMICounter);
		user_data_addr = (VAL_UINT8_T *)arg;
		ret = copy_to_user(user_data_addr, &gu4EncEMICounter, sizeof(VAL_UINT32_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_INC_ENC_EMI_USER, copy_to_user failed: %lu\n", ret);
			mutex_unlock(&EncEMILock);
			return -EFAULT;
		}
		mutex_unlock(&EncEMILock);

		MODULE_MFV_LOGD("VCODEC_INC_ENC_EMI_USER - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_DEC_ENC_EMI_USER:
	{
		MODULE_MFV_LOGD("VCODEC_DEC_ENC_EMI_USER + tid = %d\n", current->pid);

		mutex_lock(&EncEMILock);
		gu4EncEMICounter--;
		MODULE_MFV_LOGD("[VCODEC] ENC_EMI_USER = %d\n", gu4EncEMICounter);
		user_data_addr = (VAL_UINT8_T *)arg;
		ret = copy_to_user(user_data_addr, &gu4EncEMICounter, sizeof(VAL_UINT32_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_DEC_ENC_EMI_USER, copy_to_user failed: %lu\n", ret);
			mutex_unlock(&EncEMILock);
			return -EFAULT;
		}
		mutex_unlock(&EncEMILock);

		MODULE_MFV_LOGD("VCODEC_DEC_ENC_EMI_USER - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_LOCKHW:
	{
		ret = vcodec_lockhw(arg);
		if (ret) {
			pr_debug("[ERROR] VCODEC_LOCKHW failed! %lu\n", ret);
			return ret;
		}
	}
		break;

	case VCODEC_UNLOCKHW:
	{
		ret = vcodec_unlockhw(arg);
		if (ret) {
			pr_debug("[ERROR] VCODEC_UNLOCKHW failed! %lu\n", ret);
			return ret;
		}
	}
		break;

	case VCODEC_INC_PWR_USER:
	{
		MODULE_MFV_LOGD("VCODEC_INC_PWR_USER + tid = %d\n", current->pid);
		user_data_addr = (VAL_UINT8_T *)arg;
		ret = copy_from_user(&rPowerParam, user_data_addr, sizeof(VAL_POWER_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_INC_PWR_USER, copy_from_user failed: %lu\n", ret);
			return -EFAULT;
		}
		MODULE_MFV_LOGD("[VCODEC] INC_PWR_USER eDriverType = %d\n", rPowerParam.eDriverType);
		mutex_lock(&L2CLock);

#ifdef VENC_USE_L2C
		if (rPowerParam.eDriverType == VAL_DRIVER_TYPE_H264_ENC) {
			gu4L2CCounter++;
			MODULE_MFV_LOGD("[VCODEC] INC_PWR_USER L2C counter = %d\n", gu4L2CCounter);

			if (gu4L2CCounter == 1) {
				if (config_L2(0)) {
					pr_debug("[VCODEC][ERROR] Switch L2C size to 512K failed\n");
					mutex_unlock(&L2CLock);
					return -EFAULT;
				}
				pr_debug("[VCODEC] Switch L2C size to 512K successful\n");

			}
		}
#endif
		mutex_unlock(&L2CLock);
		MODULE_MFV_LOGD("VCODEC_INC_PWR_USER - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_DEC_PWR_USER:
	{
		MODULE_MFV_LOGD("VCODEC_DEC_PWR_USER + tid = %d\n", current->pid);
		user_data_addr = (VAL_UINT8_T *)arg;
		ret = copy_from_user(&rPowerParam, user_data_addr, sizeof(VAL_POWER_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_DEC_PWR_USER, copy_from_user failed: %lu\n", ret);
			return -EFAULT;
		}
		MODULE_MFV_LOGD("[VCODEC] DEC_PWR_USER eDriverType = %d\n", rPowerParam.eDriverType);

		mutex_lock(&L2CLock);

#ifdef VENC_USE_L2C
		if (rPowerParam.eDriverType == VAL_DRIVER_TYPE_H264_ENC) {
			gu4L2CCounter--;
			MODULE_MFV_LOGD("[VCODEC] DEC_PWR_USER L2C counter  = %d\n", gu4L2CCounter);

			if (gu4L2CCounter == 0) {
				if (config_L2(1)) {
					pr_debug("[VCODEC][ERROR] Switch L2C size to 0K failed\n");
					mutex_unlock(&L2CLock);
					return -EFAULT;
				}
				pr_debug("[VCODEC] Switch L2C size to 0K successful\n");
			}
		}
#endif
		mutex_unlock(&L2CLock);
		MODULE_MFV_LOGD("VCODEC_DEC_PWR_USER - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_WAITISR:
	{
		ret = vcodec_waitisr(arg);
		if (ret) {
			pr_debug("[ERROR] VCODEC_WAITISR failed! %lu\n", ret);
			return ret;
		}
	}
	break;

	case VCODEC_INITHWLOCK:
	{
		pr_debug("VCODEC_INITHWLOCK [EMPTY] + - tid = %d\n", current->pid);
		pr_debug("VCODEC_INITHWLOCK [EMPTY] - - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_DEINITHWLOCK:
	{
		pr_debug("VCODEC_DEINITHWLOCK [EMPTY] + - tid = %d\n", current->pid);
		pr_debug("VCODEC_DEINITHWLOCK [EMPTY] - - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_GET_CPU_LOADING_INFO:
	{
		VAL_UINT8_T *user_data_addr;
		VAL_VCODEC_CPU_LOADING_INFO_T _temp = {0};

		MODULE_MFV_LOGD("VCODEC_GET_CPU_LOADING_INFO +\n");
		user_data_addr = (VAL_UINT8_T *)arg;

		ret = copy_to_user(user_data_addr, &_temp, sizeof(VAL_VCODEC_CPU_LOADING_INFO_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_GET_CPU_LOADING_INFO, copy_to_user failed: %lu\n", ret);
			return -EFAULT;
		}

		MODULE_MFV_LOGD("VCODEC_GET_CPU_LOADING_INFO -\n");
	}
	break;

	case VCODEC_GET_CORE_LOADING:
	{
		MODULE_MFV_LOGD("VCODEC_GET_CORE_LOADING + - tid = %d\n", current->pid);

		user_data_addr = (VAL_UINT8_T *)arg;
		ret = copy_from_user(&rTempCoreLoading, user_data_addr, sizeof(VAL_VCODEC_CORE_LOADING_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_GET_CORE_LOADING, copy_from_user failed: %lu\n", ret);
			return -EFAULT;
		}
		if (rTempCoreLoading.CPUid > num_possible_cpus()) {
			pr_debug("[ERROR] rTempCoreLoading.CPUid(%d) > num_possible_cpus(%d)\n",
			rTempCoreLoading.CPUid, num_possible_cpus());
			return -EFAULT;
		}
		if (rTempCoreLoading.CPUid < 0) {
			pr_debug("[ERROR] rTempCoreLoading.CPUid(%d) < 0\n", rTempCoreLoading.CPUid);
			return -EFAULT;
		}
		rTempCoreLoading.Loading = get_cpu_load(rTempCoreLoading.CPUid);
		ret = copy_to_user(user_data_addr, &rTempCoreLoading, sizeof(VAL_VCODEC_CORE_LOADING_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_GET_CORE_LOADING, copy_to_user failed: %lu\n", ret);
			return -EFAULT;
		}
		MODULE_MFV_LOGD("VCODEC_GET_CORE_LOADING - - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_GET_CORE_NUMBER:
	{
		MODULE_MFV_LOGD("VCODEC_GET_CORE_NUMBER + - tid = %d\n", current->pid);

		user_data_addr = (VAL_UINT8_T *)arg;
		temp_nr_cpu_ids = nr_cpu_ids;
		ret = copy_to_user(user_data_addr, &temp_nr_cpu_ids, sizeof(int));
		if (ret) {
			pr_debug("[ERROR] VCODEC_GET_CORE_NUMBER, copy_to_user failed: %lu\n", ret);
			return -EFAULT;
		}
		MODULE_MFV_LOGD("VCODEC_GET_CORE_NUMBER - - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_SET_CPU_OPP_LIMIT:
	{
		pr_debug("VCODEC_SET_CPU_OPP_LIMIT [EMPTY] + - tid = %d\n", current->pid);
		user_data_addr = (VAL_UINT8_T *)arg;
		ret = copy_from_user(&rCpuOppLimit, user_data_addr, sizeof(VAL_VCODEC_CPU_OPP_LIMIT_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_SET_CPU_OPP_LIMIT, copy_from_user failed: %lu\n", ret);
			return -EFAULT;
		}
		pr_debug("+VCODEC_SET_CPU_OPP_LIMIT (%d, %d, %d), tid = %d\n",
			rCpuOppLimit.limited_freq, rCpuOppLimit.limited_cpu, rCpuOppLimit.enable, current->pid);
		/* TODO: Check if cpu_opp_limit is available */
		/*
		 * ret = cpu_opp_limit(EVENT_VIDEO, rCpuOppLimit.limited_freq,
		 * rCpuOppLimit.limited_cpu, rCpuOppLimit.enable); // 0: PASS, other: FAIL
		 * if (ret) {
		 * pr_debug("[VCODEC][ERROR] cpu_opp_limit failed: %lu\n", ret);
		 *	return -EFAULT;
		 * }
		 */
		pr_debug("-VCODEC_SET_CPU_OPP_LIMIT tid = %d, ret = %lu\n", current->pid, ret);
		pr_debug("VCODEC_SET_CPU_OPP_LIMIT [EMPTY] - - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_MB:
	{
		/* MB Reason: make sure register order is correct */
		mb();
	}
	break;

	case VCODEC_SET_LOG_COUNT:
	{
		MODULE_MFV_LOGD("VCODEC_SET_LOG_COUNT + tid = %d\n", current->pid);

		mutex_lock(&LogCountLock);
		user_data_addr = (VAL_UINT8_T *)arg;
		ret = copy_from_user(&rIncLogCount, user_data_addr, sizeof(VAL_BOOL_T));
		if (ret) {
			pr_debug("[ERROR] VCODEC_SET_LOG_COUNT, copy_from_user failed: %lu\n", ret);
			mutex_unlock(&LogCountLock);
			return -EFAULT;
		}

		if (rIncLogCount == VAL_TRUE) {
			if (gu4LogCountUser == 0) {
				gu4LogCount = get_detect_count();
				set_detect_count(gu4LogCount + 100);
			}
			gu4LogCountUser++;
		} else {
			gu4LogCountUser--;
			if (gu4LogCountUser == 0) {
				set_detect_count(gu4LogCount);
				gu4LogCount = 0;
			}
		}
		mutex_unlock(&LogCountLock);

		MODULE_MFV_LOGD("VCODEC_SET_LOG_COUNT - tid = %d\n", current->pid);
	}
	break;

	case VCODEC_SET_FRAME_INFO:
	{
		ret = vcodec_set_frame_info(arg);
		if (ret) {
			MODULE_MFV_LOGD("[ERROR] VCODEC_SET_FRAME_INFO failed! %lu\n", ret);
			return ret;
		}
	}
	break;
	default:
	{
		pr_debug("========[ERROR] vcodec_ioctl default case======== %u\n", cmd);
	}
	break;

	}
	return 0xFF;
}

#if IS_ENABLED(CONFIG_COMPAT)

enum STRUCT_TYPE {
	VAL_HW_LOCK_TYPE = 0,
	VAL_POWER_TYPE,
	VAL_ISR_TYPE,
	VAL_MEMORY_TYPE,
	VAL_FRAME_INFO_TYPE
};

enum COPY_DIRECTION {
	COPY_FROM_USER = 0,
	COPY_TO_USER,
};

struct COMPAT_VAL_HW_LOCK_T {
	/* [IN]     The video codec driver handle */
	compat_uptr_t       pvHandle;
	/* [IN]     The size of video codec driver handle */
	compat_uint_t       u4HandleSize;
	/* [IN/OUT] The Lock discriptor */
	compat_uptr_t       pvLock;
	/* [IN]     The timeout ms */
	compat_uint_t       u4TimeoutMs;
	/* [IN/OUT] The reserved parameter */
	compat_uptr_t       pvReserved;
	/* [IN]     The size of reserved parameter structure */
	compat_uint_t       u4ReservedSize;
	/* [IN]     The driver type */
	compat_uint_t       eDriverType;
	/* [IN]     True if this is a secure instance // MTK_SEC_VIDEO_PATH_SUPPORT */
	char                bSecureInst;
};

struct COMPAT_VAL_POWER_T {
	/* [IN]     The video codec driver handle */
	compat_uptr_t       pvHandle;
	/* [IN]     The size of video codec driver handle */
	compat_uint_t       u4HandleSize;
	/* [IN]     The driver type */
	compat_uint_t       eDriverType;
	/* [IN]     Enable or not. */
	char                fgEnable;
	/* [IN/OUT] The reserved parameter */
	compat_uptr_t       pvReserved;
	/* [IN]     The size of reserved parameter structure */
	compat_uint_t       u4ReservedSize;
	/* [OUT]    The number of power user right now */
	/* VAL_UINT32_T        u4L2CUser; */
};

struct COMPAT_VAL_ISR_T {
	/* [IN]     The video codec driver handle */
	compat_uptr_t       pvHandle;
	/* [IN]     The size of video codec driver handle */
	compat_uint_t       u4HandleSize;
	/* [IN]     The driver type */
	compat_uint_t       eDriverType;
	/* [IN]     The isr function */
	compat_uptr_t       pvIsrFunction;
	/* [IN/OUT] The reserved parameter */
	compat_uptr_t       pvReserved;
	/* [IN]     The size of reserved parameter structure */
	compat_uint_t       u4ReservedSize;
	/* [IN]     The timeout in ms */
	compat_uint_t       u4TimeoutMs;
	/* [IN]     The num of return registers when HW done */
	compat_uint_t       u4IrqStatusNum;
	/* [IN/OUT] The value of return registers when HW done */
	compat_uint_t       u4IrqStatus[IRQ_STATUS_MAX_NUM];
};

struct COMPAT_VAL_MEMORY_T {
	/* [IN]     The allocation memory type */
	compat_uint_t       eMemType;
	/* [IN]     The size of memory allocation */
	compat_ulong_t      u4MemSize;
	/* [IN/OUT] The memory virtual address */
	compat_uptr_t       pvMemVa;
	/* [IN/OUT] The memory physical address */
	compat_uptr_t       pvMemPa;
	/* [IN]     The memory byte alignment setting */
	compat_uint_t       eAlignment;
	/* [IN/OUT] The align memory virtual address */
	compat_uptr_t       pvAlignMemVa;
	/* [IN/OUT] The align memory physical address */
	compat_uptr_t       pvAlignMemPa;
	/* [IN]     The memory codec for VENC or VDEC */
	compat_uint_t       eMemCodec;
	compat_uint_t       i4IonShareFd;
	compat_uptr_t       pIonBufhandle;
	/* [IN/OUT] The reserved parameter */
	compat_uptr_t       pvReserved;
	/* [IN]     The size of reserved parameter structure */
	compat_ulong_t      u4ReservedSize;
};

struct COMPAT_VAL_FRAME_INFO_T {
	compat_uptr_t handle;
	compat_uint_t driver_type;
	compat_uint_t input_size;
	compat_uint_t frame_width;
	compat_uint_t frame_height;
	compat_uint_t frame_type;
	compat_uint_t is_compressed;
};

static int get_uptr_to_32(compat_uptr_t *p, void __user **uptr)
{
	void __user *p2p;
	int err = get_user(p2p, uptr);
	*p = ptr_to_compat(p2p);
	return err;
}
static int compat_copy_struct(
			enum STRUCT_TYPE eType,
			enum COPY_DIRECTION eDirection,
			void __user *data32,
			void __user *data)
{
	compat_uint_t u;
	compat_ulong_t l;
	compat_uptr_t p;
	char c;
	int err = 0;

	switch (eType) {
	case VAL_HW_LOCK_TYPE:
	{
		if (eDirection == COPY_FROM_USER) {
			struct COMPAT_VAL_HW_LOCK_T __user *from32 = (struct COMPAT_VAL_HW_LOCK_T *)data32;
			VAL_HW_LOCK_T __user *to = (VAL_HW_LOCK_T *)data;

			err = get_user(p, &(from32->pvHandle));
			err |= put_user(compat_ptr(p), &(to->pvHandle));
			err |= get_user(u, &(from32->u4HandleSize));
			err |= put_user(u, &(to->u4HandleSize));
			err |= get_user(p, &(from32->pvLock));
			err |= put_user(compat_ptr(p), &(to->pvLock));
			err |= get_user(u, &(from32->u4TimeoutMs));
			err |= put_user(u, &(to->u4TimeoutMs));
			err |= get_user(p, &(from32->pvReserved));
			err |= put_user(compat_ptr(p), &(to->pvReserved));
			err |= get_user(u, &(from32->u4ReservedSize));
			err |= put_user(u, &(to->u4ReservedSize));
			err |= get_user(u, &(from32->eDriverType));
			err |= put_user(u, &(to->eDriverType));
			err |= get_user(c, &(from32->bSecureInst));
			err |= put_user(c, &(to->bSecureInst));
		} else {
			struct COMPAT_VAL_HW_LOCK_T __user *to32 = (struct COMPAT_VAL_HW_LOCK_T *)data32;
			VAL_HW_LOCK_T __user *from = (VAL_HW_LOCK_T *)data;

			err = get_uptr_to_32(&p, &(from->pvHandle));
			err |= put_user(p, &(to32->pvHandle));
			err |= get_user(u, &(from->u4HandleSize));
			err |= put_user(u, &(to32->u4HandleSize));
			err |= get_uptr_to_32(&p, &(from->pvLock));
			err |= put_user(p, &(to32->pvLock));
			err |= get_user(u, &(from->u4TimeoutMs));
			err |= put_user(u, &(to32->u4TimeoutMs));
			err |= get_uptr_to_32(&p, &(from->pvReserved));
			err |= put_user(p, &(to32->pvReserved));
			err |= get_user(u, &(from->u4ReservedSize));
			err |= put_user(u, &(to32->u4ReservedSize));
			err |= get_user(u, &(from->eDriverType));
			err |= put_user(u, &(to32->eDriverType));
			err |= get_user(c, &(from->bSecureInst));
			err |= put_user(c, &(to32->bSecureInst));
		}
	}
	break;
	case VAL_POWER_TYPE:
	{
		if (eDirection == COPY_FROM_USER) {
			struct COMPAT_VAL_POWER_T __user *from32 = (struct COMPAT_VAL_POWER_T *)data32;
			VAL_POWER_T __user *to = (VAL_POWER_T *)data;

			err = get_user(p, &(from32->pvHandle));
			err |= put_user(compat_ptr(p), &(to->pvHandle));
			err |= get_user(u, &(from32->u4HandleSize));
			err |= put_user(u, &(to->u4HandleSize));
			err |= get_user(u, &(from32->eDriverType));
			err |= put_user(u, &(to->eDriverType));
			err |= get_user(c, &(from32->fgEnable));
			err |= put_user(c, &(to->fgEnable));
			err |= get_user(p, &(from32->pvReserved));
			err |= put_user(compat_ptr(p), &(to->pvReserved));
			err |= get_user(u, &(from32->u4ReservedSize));
			err |= put_user(u, &(to->u4ReservedSize));
		} else {
			struct COMPAT_VAL_POWER_T __user *to32 = (struct COMPAT_VAL_POWER_T *)data32;
			VAL_POWER_T __user *from = (VAL_POWER_T *)data;

			err = get_uptr_to_32(&p, &(from->pvHandle));
			err |= put_user(p, &(to32->pvHandle));
			err |= get_user(u, &(from->u4HandleSize));
			err |= put_user(u, &(to32->u4HandleSize));
			err |= get_user(u, &(from->eDriverType));
			err |= put_user(u, &(to32->eDriverType));
			err |= get_user(c, &(from->fgEnable));
			err |= put_user(c, &(to32->fgEnable));
			err |= get_uptr_to_32(&p, &(from->pvReserved));
			err |= put_user(p, &(to32->pvReserved));
			err |= get_user(u, &(from->u4ReservedSize));
			err |= put_user(u, &(to32->u4ReservedSize));
		}
	}
	break;
	case VAL_ISR_TYPE:
	{
		int i = 0;

		if (eDirection == COPY_FROM_USER) {
			struct COMPAT_VAL_ISR_T __user *from32 = (struct COMPAT_VAL_ISR_T *)data32;
			VAL_ISR_T __user *to = (VAL_ISR_T *)data;

			err = get_user(p, &(from32->pvHandle));
			err |= put_user(compat_ptr(p), &(to->pvHandle));
			err |= get_user(u, &(from32->u4HandleSize));
			err |= put_user(u, &(to->u4HandleSize));
			err |= get_user(u, &(from32->eDriverType));
			err |= put_user(u, &(to->eDriverType));
			err |= get_user(p, &(from32->pvIsrFunction));
			err |= put_user(compat_ptr(p), &(to->pvIsrFunction));
			err |= get_user(p, &(from32->pvReserved));
			err |= put_user(compat_ptr(p), &(to->pvReserved));
			err |= get_user(u, &(from32->u4ReservedSize));
			err |= put_user(u, &(to->u4ReservedSize));
			err |= get_user(u, &(from32->u4TimeoutMs));
			err |= put_user(u, &(to->u4TimeoutMs));
			err |= get_user(u, &(from32->u4IrqStatusNum));
			err |= put_user(u, &(to->u4IrqStatusNum));
			for (; i < IRQ_STATUS_MAX_NUM; i++) {
				err |= get_user(u, &(from32->u4IrqStatus[i]));
				err |= put_user(u, &(to->u4IrqStatus[i]));
			}
			return err;

		} else {
			struct COMPAT_VAL_ISR_T __user *to32 = (struct COMPAT_VAL_ISR_T *)data32;
			VAL_ISR_T __user *from = (VAL_ISR_T *)data;

			err = get_uptr_to_32(&p, &(from->pvHandle));
			err |= put_user(p, &(to32->pvHandle));
			err |= get_user(u, &(from->u4HandleSize));
			err |= put_user(u, &(to32->u4HandleSize));
			err |= get_user(u, &(from->eDriverType));
			err |= put_user(u, &(to32->eDriverType));
			err |= get_uptr_to_32(&p, &(from->pvIsrFunction));
			err |= put_user(p, &(to32->pvIsrFunction));
			err |= get_uptr_to_32(&p, &(from->pvReserved));
			err |= put_user(p, &(to32->pvReserved));
			err |= get_user(u, &(from->u4ReservedSize));
			err |= put_user(u, &(to32->u4ReservedSize));
			err |= get_user(u, &(from->u4TimeoutMs));
			err |= put_user(u, &(to32->u4TimeoutMs));
			err |= get_user(u, &(from->u4IrqStatusNum));
			err |= put_user(u, &(to32->u4IrqStatusNum));
			for (; i < IRQ_STATUS_MAX_NUM; i++) {
				err |= get_user(u, &(from->u4IrqStatus[i]));
				err |= put_user(u, &(to32->u4IrqStatus[i]));
			}
		}
	}
	break;
	case VAL_MEMORY_TYPE:
	{
		if (eDirection == COPY_FROM_USER) {
			struct COMPAT_VAL_MEMORY_T __user *from32 = (struct COMPAT_VAL_MEMORY_T *)data32;
			VAL_MEMORY_T __user *to = (VAL_MEMORY_T *)data;

			err = get_user(u, &(from32->eMemType));
			err |= put_user(u, &(to->eMemType));
			err |= get_user(l, &(from32->u4MemSize));
			err |= put_user(l, &(to->u4MemSize));
			err |= get_user(p, &(from32->pvMemVa));
			err |= put_user(compat_ptr(p), &(to->pvMemVa));
			err |= get_user(p, &(from32->pvMemPa));
			err |= put_user(compat_ptr(p), &(to->pvMemPa));
			err |= get_user(u, &(from32->eAlignment));
			err |= put_user(u, &(to->eAlignment));
			err |= get_user(p, &(from32->pvAlignMemVa));
			err |= put_user(compat_ptr(p), &(to->pvAlignMemVa));
			err |= get_user(p, &(from32->pvAlignMemPa));
			err |= put_user(compat_ptr(p), &(to->pvAlignMemPa));
			err |= get_user(u, &(from32->eMemCodec));
			err |= put_user(u, &(to->eMemCodec));
			err |= get_user(u, &(from32->i4IonShareFd));
			err |= put_user(u, &(to->i4IonShareFd));
			err |= get_user(p, &(from32->pIonBufhandle));
			err |= put_user(compat_ptr(p), &(to->pIonBufhandle));
			err |= get_user(p, &(from32->pvReserved));
			err |= put_user(compat_ptr(p), &(to->pvReserved));
			err |= get_user(l, &(from32->u4ReservedSize));
			err |= put_user(l, &(to->u4ReservedSize));
			} else {
			struct COMPAT_VAL_MEMORY_T __user *to32 = (struct COMPAT_VAL_MEMORY_T *)data32;

			VAL_MEMORY_T __user *from = (VAL_MEMORY_T *)data;

			err = get_user(u, &(from->eMemType));
			err |= put_user(u, &(to32->eMemType));
			err |= get_user(l, &(from->u4MemSize));
			err |= put_user(l, &(to32->u4MemSize));
			err |= get_uptr_to_32(&p, &(from->pvMemVa));
			err |= put_user(p, &(to32->pvMemVa));
			err |= get_uptr_to_32(&p, &(from->pvMemPa));
			err |= put_user(p, &(to32->pvMemPa));
			err |= get_user(u, &(from->eAlignment));
			err |= put_user(u, &(to32->eAlignment));
			err |= get_uptr_to_32(&p, &(from->pvAlignMemVa));
			err |= put_user(p, &(to32->pvAlignMemVa));
			err |= get_uptr_to_32(&p, &(from->pvAlignMemPa));
			err |= put_user(p, &(to32->pvAlignMemPa));
			err |= get_user(u, &(from->eMemCodec));
			err |= put_user(u, &(to32->eMemCodec));
			err |= get_user(u, &(from->i4IonShareFd));
			err |= put_user(u, &(to32->i4IonShareFd));
			err |= get_uptr_to_32(&p, (void __user **)&(from->pIonBufhandle));
			err |= put_user(p, &(to32->pIonBufhandle));
			err |= get_uptr_to_32(&p, &(from->pvReserved));
			err |= put_user(p, &(to32->pvReserved));
			err |= get_user(l, &(from->u4ReservedSize));
			err |= put_user(l, &(to32->u4ReservedSize));
		}
	}
	break;
	case VAL_FRAME_INFO_TYPE:
	{
		if (eDirection == COPY_FROM_USER) {
			struct COMPAT_VAL_FRAME_INFO_T __user *from32 = (struct COMPAT_VAL_FRAME_INFO_T *)data32;
			struct VAL_FRAME_INFO_T __user *to = (struct VAL_FRAME_INFO_T *)data;

			err = get_user(p, &(from32->handle));
			err |= put_user(compat_ptr(p), &(to->handle));
			err |= get_user(u, &(from32->driver_type));
			err |= put_user(u, &(to->driver_type));
			err |= get_user(u, &(from32->input_size));
			err |= put_user(u, &(to->input_size));
			err |= get_user(u, &(from32->frame_width));
			err |= put_user(u, &(to->frame_width));
			err |= get_user(u, &(from32->frame_height));
			err |= put_user(u, &(to->frame_height));
			err |= get_user(u, &(from32->frame_type));
			err |= put_user(u, &(to->frame_type));
			err |= get_user(u, &(from32->is_compressed));
			err |= put_user(u, &(to->is_compressed));
		} else {
			struct COMPAT_VAL_FRAME_INFO_T __user *to32 = (struct COMPAT_VAL_FRAME_INFO_T *)data32;
			struct VAL_FRAME_INFO_T __user *from = (struct VAL_FRAME_INFO_T *)data;

			err = get_uptr_to_32(&p, &(from->handle));
			err |= put_user(p, &(to32->handle));
			err |= get_user(u, &(from->driver_type));
			err |= put_user(u, &(to32->driver_type));
			err |= get_user(u, &(from->input_size));
			err |= put_user(u, &(to32->input_size));
			err |= get_user(u, &(from->frame_width));
			err |= put_user(u, &(to32->frame_width));
			err |= get_user(u, &(from->frame_height));
			err |= put_user(u, &(to32->frame_height));
			err |= get_user(u, &(from->frame_type));
			err |= put_user(u, &(to32->frame_type));
			err |= get_user(u, &(from->is_compressed));
			err |= put_user(u, &(to32->is_compressed));
		}
	}
	break;
	default:
	break;
	}

	return err;
}


static long vcodec_unlocked_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	/* MODULE_MFV_LOGD("vcodec_unlocked_compat_ioctl: 0x%x\n", cmd); */
	switch (cmd) {
	case VCODEC_ALLOC_NON_CACHE_BUFFER:
	case VCODEC_FREE_NON_CACHE_BUFFER:
	{
		struct COMPAT_VAL_MEMORY_T __user *data32;
		VAL_MEMORY_T __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(VAL_MEMORY_T));
		if (data == NULL)
			return -EFAULT;

		err = compat_copy_struct(VAL_MEMORY_TYPE, COPY_FROM_USER, (void *)data32, (void *)data);
		if (err)
			return (long)err;

		ret = file->f_op->unlocked_ioctl(file, cmd, (unsigned long)data);

		err = compat_copy_struct(VAL_MEMORY_TYPE, COPY_TO_USER, (void *)data32, (void *)data);

		if (err)
			return err;
		return ret;
	}
	break;
	case VCODEC_LOCKHW:
	case VCODEC_UNLOCKHW:
	{
		struct COMPAT_VAL_HW_LOCK_T __user *data32;
		VAL_HW_LOCK_T __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(VAL_HW_LOCK_T));
		if (data == NULL)
			return -EFAULT;

		err = compat_copy_struct(VAL_HW_LOCK_TYPE, COPY_FROM_USER, (void *)data32, (void *)data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, cmd, (unsigned long)data);

		err = compat_copy_struct(VAL_HW_LOCK_TYPE, COPY_TO_USER, (void *)data32, (void *)data);

		if (err)
			return err;
		return ret;
	}
	break;

	case VCODEC_INC_PWR_USER:
	case VCODEC_DEC_PWR_USER:
	{
		struct COMPAT_VAL_POWER_T __user *data32;
		VAL_POWER_T __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(VAL_POWER_T));
		if (data == NULL)
			return -EFAULT;

		err = compat_copy_struct(VAL_POWER_TYPE, COPY_FROM_USER, (void *)data32, (void *)data);

		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, cmd, (unsigned long)data);

		err = compat_copy_struct(VAL_POWER_TYPE, COPY_TO_USER, (void *)data32, (void *)data);

		if (err)
			return err;
		return ret;
	}
	break;

	case VCODEC_WAITISR:
	{
		struct COMPAT_VAL_ISR_T __user *data32;
		VAL_ISR_T __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(VAL_ISR_T));
		if (data == NULL)
			return -EFAULT;

		err = compat_copy_struct(VAL_ISR_TYPE, COPY_FROM_USER, (void *)data32, (void *)data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, VCODEC_WAITISR, (unsigned long)data);

		err = compat_copy_struct(VAL_ISR_TYPE, COPY_TO_USER, (void *)data32, (void *)data);

		if (err)
			return err;
		return ret;
	}
	break;

	case VCODEC_SET_FRAME_INFO:
	{
		struct COMPAT_VAL_FRAME_INFO_T __user *data32;
		struct VAL_FRAME_INFO_T __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(struct VAL_FRAME_INFO_T));
		if (data == NULL)
			return -EFAULT;

		err = compat_copy_struct(VAL_FRAME_INFO_TYPE, COPY_FROM_USER, (void *)data32, (void *)data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, VCODEC_SET_FRAME_INFO, (unsigned long)data);

		err = compat_copy_struct(VAL_FRAME_INFO_TYPE, COPY_TO_USER, (void *)data32, (void *)data);

		if (err)
			return err;
	}

	default:
		return vcodec_unlocked_ioctl(file, cmd, arg);
	}
	return 0;
}
#else
#define vcodec_unlocked_compat_ioctl NULL
#endif
static int vcodec_open(struct inode *inode, struct file *file)
{
	MODULE_MFV_LOGD("vcodec_open\n");

	mutex_lock(&DriverOpenCountLock);
	Driver_Open_Count++;

	MODULE_MFV_LOGD("vcodec_open pid = %d, Driver_Open_Count %d\n", current->pid, Driver_Open_Count);
	mutex_unlock(&DriverOpenCountLock);

	/* TODO: Check upper limit of concurrent users? */

	return 0;
}

static int vcodec_flush(struct file *file, fl_owner_t id)
{
	MODULE_MFV_LOGD("vcodec_flush, curr_tid =%d\n", current->pid);
	MODULE_MFV_LOGD("vcodec_flush pid = %d, Driver_Open_Count %d\n", current->pid, Driver_Open_Count);

	return 0;
}

static int vcodec_release(struct inode *inode, struct file *file)
{
	VAL_ULONG_T ulFlagsLockHW, ulFlagsISR;
	VAL_VOID_T *pvCheckHandle = 0;

	/* dump_stack(); */
	MODULE_MFV_LOGD("vcodec_release, curr_tid =%d\n", current->pid);
	mutex_lock(&DriverOpenCountLock);
	MODULE_MFV_LOGD("vcodec_release pid = %d, Driver_Open_Count %d\n", current->pid, Driver_Open_Count);
	Driver_Open_Count--;

	if (Driver_Open_Count == 0) {
		mutex_lock(&VdecHWLock);
		gu4VdecLockThreadId = 0;
		pvCheckHandle = grVcodecDecHWLock.pvHandle;

		/* check if someone didn't unlockHW */
		if (grVcodecDecHWLock.pvHandle != 0) {
			pr_info("[ERROR] someone didn't unlockHW vcodec_release pid = %d, grVcodecDecHWLock.eDriverType %d grVcodecDecHWLock.pvHandle 0x%lx\n",
				current->pid, grVcodecDecHWLock.eDriverType, (VAL_ULONG_T)grVcodecDecHWLock.pvHandle);

			/* power off */
			if (grVcodecDecHWLock.eDriverType == VAL_DRIVER_TYPE_MP4_DEC ||
				grVcodecDecHWLock.eDriverType == VAL_DRIVER_TYPE_HEVC_DEC ||
				grVcodecDecHWLock.eDriverType == VAL_DRIVER_TYPE_H264_DEC ||
				grVcodecDecHWLock.eDriverType == VAL_DRIVER_TYPE_MP1_MP2_DEC) {
				vdec_break();
				pr_debug("[WARNING] VCODEC_DEC release, reset power/irq!!\n");
#ifdef CONFIG_PM
				pm_runtime_put_sync(vcodec_device);
#else
#ifndef KS_POWER_WORKAROUND
				vdec_power_off();
#endif
#endif
				disable_irq(VDEC_IRQ_ID);
			}
		}

		grVcodecDecHWLock.pvHandle = 0;
		grVcodecDecHWLock.eDriverType = VAL_DRIVER_TYPE_NONE;
		grVcodecDecHWLock.rLockedTime.u4Sec = 0;
		grVcodecDecHWLock.rLockedTime.u4uSec = 0;
		mutex_unlock(&VdecHWLock);
		if (pvCheckHandle != 0)
			eVideoSetEvent(&DecHWLockEvent, sizeof(VAL_EVENT_T));

		mutex_lock(&VencHWLock);
		pvCheckHandle = grVcodecEncHWLock.pvHandle;
		if (grVcodecEncHWLock.pvHandle != 0) {
			pr_info("[ERROR] someone didn't unlockHW vcodec_release pid = %d, grVcodecEncHWLock.eDriverType %d grVcodecEncHWLock.pvHandle 0x%lx\n",
				current->pid, grVcodecEncHWLock.eDriverType, (VAL_ULONG_T)grVcodecEncHWLock.pvHandle);
			if (grVcodecEncHWLock.eDriverType == VAL_DRIVER_TYPE_H264_ENC) {
				venc_break();
				pr_debug("[WARNING] VCODEC_ENC release, reset power/irq!!\n");
#ifdef CONFIG_PM
				pm_runtime_put_sync(vcodec_device2);
#else
#ifndef KS_POWER_WORKAROUND
				venc_power_off();
#endif
#endif
				disable_irq(VENC_IRQ_ID);
			}
		}
		grVcodecEncHWLock.pvHandle = 0;
		grVcodecEncHWLock.eDriverType = VAL_DRIVER_TYPE_NONE;
		grVcodecEncHWLock.rLockedTime.u4Sec = 0;
		grVcodecEncHWLock.rLockedTime.u4uSec = 0;
		mutex_unlock(&VencHWLock);
		if (pvCheckHandle != 0)
			eVideoSetEvent(&EncHWLockEvent, sizeof(VAL_EVENT_T));

		mutex_lock(&DecEMILock);
		gu4DecEMICounter = 0;
		mutex_unlock(&DecEMILock);

		mutex_lock(&EncEMILock);
		gu4EncEMICounter = 0;
		mutex_unlock(&EncEMILock);

		mutex_lock(&PWRLock);
		gu4PWRCounter = 0;
		mutex_unlock(&PWRLock);

#if defined(VENC_USE_L2C)
		mutex_lock(&L2CLock);
		if (gu4L2CCounter != 0) {
			pr_debug("vcodec_flush pid = %d, L2 user = %d, force restore L2 settings\n",
				 current->pid, gu4L2CCounter);
			if (config_L2(1)) {
				/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
				pr_debug("[VCODEC][ERROR] restore L2 settings failed\n");
			}
		}
		gu4L2CCounter = 0;
		mutex_unlock(&L2CLock);
#endif
#ifdef VCODEC_DVFS_V2
		mutex_lock(&VdecDVFSLock);
		free_hist(&dec_hists, 0);
		mutex_unlock(&VdecDVFSLock);
#endif

		spin_lock_irqsave(&LockDecHWCountLock, ulFlagsLockHW);
		gu4LockDecHWCount = 0;
		spin_unlock_irqrestore(&LockDecHWCountLock, ulFlagsLockHW);

		spin_lock_irqsave(&LockEncHWCountLock, ulFlagsLockHW);
		gu4LockEncHWCount = 0;
		spin_unlock_irqrestore(&LockEncHWCountLock, ulFlagsLockHW);

		spin_lock_irqsave(&DecISRCountLock, ulFlagsISR);
		gu4DecISRCount = 0;
		spin_unlock_irqrestore(&DecISRCountLock, ulFlagsISR);

		spin_lock_irqsave(&EncISRCountLock, ulFlagsISR);
		gu4EncISRCount = 0;
		spin_unlock_irqrestore(&EncISRCountLock, ulFlagsISR);

#ifdef ENABLE_MMDVFS_VDEC
		if (gMMDFVFSMonitorStarts == VAL_TRUE) {
			gMMDFVFSMonitorStarts = VAL_FALSE;
			gMMDFVFSMonitorCounts = 0;
			gHWLockInterval = 0;
			gHWLockMaxDuration = 0;
			SendDvfsRequest(DVFS_UNREQUEST);
		}
#endif
	}
	mutex_unlock(&DriverOpenCountLock);

	return 0;
}

void vcodec_vma_open(struct vm_area_struct *vma)
{
	MODULE_MFV_LOGD("vcodec VMA open, virt %lx, phys %lx\n", vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
}

void vcodec_vma_close(struct vm_area_struct *vma)
{
	MODULE_MFV_LOGD("vcodec VMA close, virt %lx, phys %lx\n", vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
}

static struct vm_operations_struct vcodec_remap_vm_ops = {
	.open = vcodec_vma_open,
	.close = vcodec_vma_close,
};

static int vcodec_mmap(struct file *file, struct vm_area_struct *vma)
{
#if 1
	VAL_UINT32_T u4I = 0;
	VAL_ULONG_T length;
	VAL_ULONG_T pfn;

	length = vma->vm_end - vma->vm_start;
	pfn = vma->vm_pgoff<<PAGE_SHIFT;

	if (((length > VENC_REGION) || (pfn < VENC_BASE) || (pfn > VENC_BASE+VENC_REGION)) &&
	   ((length > VDEC_REGION) || (pfn < VDEC_BASE_PHY) || (pfn > VDEC_BASE_PHY+VDEC_REGION)) &&
	   ((length > HW_REGION) || (pfn < HW_BASE) || (pfn > HW_BASE+HW_REGION)) &&
	   ((length > INFO_REGION) || (pfn < INFO_BASE) || (pfn > INFO_BASE+INFO_REGION))
	) {
		VAL_ULONG_T ulAddr, ulSize;

		for (u4I = 0; u4I < VCODEC_MULTIPLE_INSTANCE_NUM_x_10; u4I++) {
			if ((grNonCacheMemoryList[u4I].ulKVA != -1L) && (grNonCacheMemoryList[u4I].ulKPA != -1L)) {
				ulAddr = grNonCacheMemoryList[u4I].ulKPA;
				ulSize = (grNonCacheMemoryList[u4I].ulSize + 0x1000 - 1) & ~(0x1000 - 1);
				if ((length == ulSize) && (pfn == ulAddr)) {
					MODULE_MFV_LOGD("[VCODEC] cache idx %d\n", u4I);
					break;
				}
			}
		}

		if (u4I == VCODEC_MULTIPLE_INSTANCE_NUM_x_10) {
			pr_debug("[VCODEC][ERROR] mmap region error: Length(0x%lx), pfn(0x%lx)\n",
				 (VAL_ULONG_T)length, pfn);
			return -EAGAIN;
		}
	}
#endif
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	MODULE_MFV_LOGD("[VCODEC][mmap] vma->start 0x%lx, vma->end 0x%lx, vma->pgoff 0x%lx\n",
			 (VAL_ULONG_T)vma->vm_start, (VAL_ULONG_T)vma->vm_end, (VAL_ULONG_T)vma->vm_pgoff);
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
		vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		return -EAGAIN;
	}

	vma->vm_ops = &vcodec_remap_vm_ops;
	vcodec_vma_open(vma);

	return 0;
}

static const struct file_operations vcodec_fops = {
	.owner      = THIS_MODULE,
	.unlocked_ioctl = vcodec_unlocked_ioctl,
	.open       = vcodec_open,
	.flush      = vcodec_flush,
	.release    = vcodec_release,
	.mmap       = vcodec_mmap,
#if IS_ENABLED(CONFIG_COMPAT)
	.compat_ioctl   = vcodec_unlocked_compat_ioctl,
#endif

};

#if USE_WAKELOCK == 0
/**
 * Suspsend callbacks after user space processes are frozen
 * Since user space processes are frozen, there is no need and cannot hold same
 * mutex that protects lock owner while checking status.
 * If video codec hardware is still active now, must not aenter suspend.
 **/
static int vcodec_suspend(struct platform_device *pDev, pm_message_t state)
{
	if (grVcodecDecHWLock.pvHandle != 0 || grVcodecEncHWLock.pvHandle != 0) {
		MODULE_MFV_PR_DEBUG("vcodec_suspend fail due to videocodec active");
		return -EBUSY;
	}
	MODULE_MFV_PR_DEBUG("vcodec_suspend ok");
	return 0;
}

static int vcodec_resume(struct platform_device *pDev)
{
	MODULE_MFV_PR_DEBUG("vcodec_resume ok");
	return 0;
}

/**
 * Suspend notifiers before user space processes are frozen.
 * User space driver can still complete decoding/encoding of current frame.
 * Change state to is_entering_suspend to stop further tasks but allow current
 * frame to complete (LOCKHW, WAITISR, UNLOCKHW).
 * Since there is no critical section proection, it is possible for a new task
 * to start after changing to is_entering_suspend state. This case will be
 * handled by suspend callback vcodec_suspend.
 **/
static int vcodec_suspend_notifier(struct notifier_block *nb, unsigned long action, void *data)
{
	int wait_cnt = 0;

	MODULE_MFV_PR_DEBUG("vcodec_suspend_notifier ok action = %ld", action);
	switch (action) {
	case PM_SUSPEND_PREPARE:
		is_entering_suspend = 1;
		while (grVcodecDecHWLock.pvHandle != 0 || grVcodecEncHWLock.pvHandle != 0) {
			wait_cnt++;
			if (wait_cnt > 90) {
				MODULE_MFV_LOGD("vcodec_pm_suspend waiting for vcodec inactive %p %p",
						grVcodecDecHWLock.pvHandle, grVcodecEncHWLock.pvHandle);
				return NOTIFY_DONE;
			}
			msleep(1);
		}
		return NOTIFY_OK;
	case PM_POST_SUSPEND:
		is_entering_suspend = 0;
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
	return NOTIFY_DONE;
}
#endif

#ifdef CONFIG_PM
static struct dev_pm_domain mt_vdec_pm_domain = {
	.ops = {
		SET_RUNTIME_PM_OPS(mt_vdec_runtime_suspend,
				mt_vdec_runtime_resume,
				NULL)
		}
};

static struct dev_pm_domain mt_venc_pm_domain = {
	.ops = {
		SET_RUNTIME_PM_OPS(mt_venc_runtime_suspend,
				mt_venc_runtime_resume,
				NULL)
		}
};
#endif

static int vcodec_probe(struct platform_device *dev)
{
	int ret;

	MODULE_MFV_LOGD("+vcodec_probe\n");

	mutex_lock(&VdecPWRLock);
	gi4DecWaitEMI = 0;
	mutex_unlock(&VdecPWRLock);

	mutex_lock(&DecEMILock);
	gu4DecEMICounter = 0;
	mutex_unlock(&DecEMILock);

	mutex_lock(&EncEMILock);
	gu4EncEMICounter = 0;
	mutex_unlock(&EncEMILock);

	mutex_lock(&PWRLock);
	gu4PWRCounter = 0;
	mutex_unlock(&PWRLock);

	mutex_lock(&L2CLock);
	gu4L2CCounter = 0;
	mutex_unlock(&L2CLock);

	ret = register_chrdev_region(vcodec_devno, 1, VCODEC_DEVNAME);
	if (ret) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[ERROR] Can't Get Major number for VCodec Device\n");
	}

	vcodec_cdev = cdev_alloc();
	vcodec_cdev->owner = THIS_MODULE;
	vcodec_cdev->ops = &vcodec_fops;

	ret = cdev_add(vcodec_cdev, vcodec_devno, 1);
	if (ret) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[ERROR] Can't add Vcodec Device\n");
	}

	vcodec_class = class_create(THIS_MODULE, VCODEC_DEVNAME);
	if (IS_ERR(vcodec_class)) {
		ret = PTR_ERR(vcodec_class);
		pr_debug("[VCODEC][ERROR] Unable to create class, err = %d", ret);
		return ret;
	}

	vcodec_device = device_create(vcodec_class, NULL, vcodec_devno, NULL, VCODEC_DEVNAME);
#ifdef CONFIG_PM
	vcodec_device->pm_domain = &mt_vdec_pm_domain;

	vcodec_cdev2 = cdev_alloc();
	vcodec_cdev2->owner = THIS_MODULE;
	vcodec_cdev2->ops = &vcodec_fops;

	ret = cdev_add(vcodec_cdev2, vcodec_devno2, 1);
	if (ret) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		MODULE_MFV_LOGD("[ERROR] Can't add Vcodec Device 2\n");
	}

	vcodec_class2 = class_create(THIS_MODULE, VCODEC_DEVNAME2);
	if (IS_ERR(vcodec_class2)) {
		ret = PTR_ERR(vcodec_class2);
		MODULE_MFV_LOGD("[VCODEC][ERROR] Unable to create class 2, err = %d", ret);
		return ret;
	}

	vcodec_device2 = device_create(vcodec_class2, NULL, vcodec_devno2, NULL, VCODEC_DEVNAME2);
	vcodec_device2->pm_domain = &mt_venc_pm_domain;

	pm_runtime_enable(vcodec_device);
	pm_runtime_enable(vcodec_device2);
#endif
#ifdef CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT
#else
	if (request_irq(VDEC_IRQ_ID, (irq_handler_t)video_intr_dlr, IRQF_TRIGGER_LOW, VCODEC_DEVNAME, NULL) < 0) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] error to request dec irq\n");
	} else {
		MODULE_MFV_LOGD("[VCODEC] success to request dec irq: %d\n", VDEC_IRQ_ID);
	}

	if (request_irq(VENC_IRQ_ID, (irq_handler_t)video_intr_dlr2, IRQF_TRIGGER_LOW, VCODEC_DEVNAME, NULL) < 0) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		MODULE_MFV_LOGD("[VCODEC][ERROR] error to request enc irq\n");
	} else {
		MODULE_MFV_LOGD("[VCODEC] success to request enc irq: %d\n", VENC_IRQ_ID);
	}

	disable_irq(VDEC_IRQ_ID);
	disable_irq(VENC_IRQ_ID);
#endif
#if 0
#ifndef CONFIG_MTK_SMI_EXT
	clk_MT_CG_SMI_COMMON = devm_clk_get(&dev->dev, "MT_CG_SMI_COMMON");
	if (IS_ERR(clk_MT_CG_SMI_COMMON)) {
		pr_debug("[VCODEC][ERROR] Unable to devm_clk_get MT_CG_SMI_COMMON\n");
		return PTR_ERR(clk_MT_CG_SMI_COMMON);
	}

	clk_MT_CG_GALS_VDEC2MM = devm_clk_get(&dev->dev, "MT_CG_GALS_VDEC2MM");
	if (IS_ERR(clk_MT_CG_GALS_VDEC2MM)) {
		pr_debug("[VCODEC][ERROR] Unable to devm_clk_get MT_CG_GALS_VDEC2MM\n");
		return PTR_ERR(clk_MT_CG_GALS_VDEC2MM);
	}

	clk_MT_CG_GALS_VENC2MM = devm_clk_get(&dev->dev, "MT_CG_GALS_VENC2MM");
	if (IS_ERR(clk_MT_CG_GALS_VENC2MM)) {
		pr_debug("[VCODEC][ERROR] Unable to devm_clk_get MT_CG_GALS_VENC2MM\n");
		return PTR_ERR(clk_MT_CG_GALS_VENC2MM);
	}
#endif
#endif

	clk_MT_CG_VDEC = devm_clk_get(&dev->dev, "MT_CG_VDEC");
	if (IS_ERR(clk_MT_CG_VDEC)) {
		pr_debug("[VCODEC][ERROR] Unable to devm_clk_get MT_CG_VDEC\n");
		return PTR_ERR(clk_MT_CG_VDEC);
	}

	clk_MT_CG_VENC_VENC = devm_clk_get(&dev->dev, "MT_CG_VENC");
	if (IS_ERR(clk_MT_CG_VENC_VENC)) {
		pr_debug("[VCODEC][ERROR] Unable to devm_clk_get MT_CG_VENC_VENC\n");
		return PTR_ERR(clk_MT_CG_VENC_VENC);
	}

	clk_MT_SCP_SYS_VDE = devm_clk_get(&dev->dev, "MT_SCP_SYS_VDE");
	if (IS_ERR(clk_MT_SCP_SYS_VDE)) {
		pr_debug("[VCODEC][ERROR] Unable to devm_clk_get MT_SCP_SYS_VDE\n");
		return PTR_ERR(clk_MT_SCP_SYS_VDE);
	}

	clk_MT_SCP_SYS_VEN = devm_clk_get(&dev->dev, "MT_SCP_SYS_VEN");
	if (IS_ERR(clk_MT_SCP_SYS_VEN)) {
		pr_debug("[VCODEC][ERROR] Unable to devm_clk_get MT_SCP_SYS_VEN\n");
		return PTR_ERR(clk_MT_SCP_SYS_VEN);
	}

	clk_MT_SCP_SYS_DIS = devm_clk_get(&dev->dev, "MT_SCP_SYS_DIS");
	if (IS_ERR(clk_MT_SCP_SYS_DIS)) {
		pr_debug("[VCODEC][ERROR] Unable to devm_clk_get MT_SCP_SYS_DIS\n");
		return PTR_ERR(clk_MT_SCP_SYS_DIS);
	}

#if USE_WAKELOCK == 0
	pm_notifier(vcodec_suspend_notifier, 0);
#endif

	MODULE_MFV_LOGD("vcodec_probe Done\n");

#ifdef KS_POWER_WORKAROUND
	vdec_power_on();
	venc_power_on();
#endif
#ifdef CONFIG_MTK_QOS_SUPPORT
	pm_qos_add_request(&vcodec_qos_request, PM_QOS_MM_MEMORY_BANDWIDTH, PM_QOS_DEFAULT_VALUE);
	pm_qos_add_request(&vcodec_qos_request2, PM_QOS_MM_MEMORY_BANDWIDTH, PM_QOS_DEFAULT_VALUE);
	pm_qos_add_request(&vcodec_qos_request_f, PM_QOS_VDEC_FREQ, PM_QOS_DEFAULT_VALUE);
	pm_qos_add_request(&vcodec_qos_request_f2, PM_QOS_VENC_FREQ, PM_QOS_DEFAULT_VALUE);
	snprintf(vcodec_qos_request.owner, sizeof(vcodec_qos_request.owner) - 1, "vdec_bw");
	snprintf(vcodec_qos_request2.owner, sizeof(vcodec_qos_request2.owner) - 1, "venc_bw");
	snprintf(vcodec_qos_request_f.owner, sizeof(vcodec_qos_request_f.owner) - 1, "vdec_freq");
	snprintf(vcodec_qos_request_f2.owner, sizeof(vcodec_qos_request_f2.owner) - 1, "venc_freq");

#endif

	dec_step_size = 1;
	enc_step_size = 1;
	ret = mmdvfs_qos_get_freq_steps(PM_QOS_VDEC_FREQ, &g_dec_freq_steps[0], &dec_step_size);
	if (ret < 0)
		pr_debug("Video decoder  get MMDVFS freq steps failed, result: %d\n", ret);

	ret = mmdvfs_qos_get_freq_steps(PM_QOS_VENC_FREQ, &g_enc_freq_steps[0], &enc_step_size);
	if (ret < 0)
		pr_debug("Video encoder  get MMDVFS freq steps failed, result: %d\n", ret);
	return 0;
}
static int venc_disableIRQ(VAL_HW_LOCK_T *prHWLock)
{
	VAL_UINT32_T  u4IrqId = VENC_IRQ_ID;
#ifdef CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT	/* Morris Yang moved to TEE */
	if (prHWLock->bSecureInst == VAL_FALSE) {
		/* disable_irq(VENC_IRQ_ID); */

		free_irq(u4IrqId, NULL);
	}
#else
		/* disable_irq(MT_VENC_IRQ_ID); */
		disable_irq(u4IrqId);
#endif
		/* turn venc power off */
#ifdef CONFIG_PM
		pm_runtime_put_sync(vcodec_device2);
#else
#ifndef KS_POWER_WORKAROUND
		venc_power_off();
#endif
#endif

		return 0;
}

static int venc_enableIRQ(VAL_HW_LOCK_T *prHWLock)
{
	VAL_UINT32_T  u4IrqId = VENC_IRQ_ID;

	MODULE_MFV_LOGD("venc_enableIRQ+\n");
#ifdef CONFIG_PM
	pm_runtime_get_sync(vcodec_device2);
#else
#ifndef KS_POWER_WORKAROUND
	venc_power_on();
#endif
#endif

#ifdef CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT
	MODULE_MFV_LOGD("[VCODEC_LOCKHW] ENC rHWLock.bSecureInst 0x%x\n", prHWLock->bSecureInst);
	if (prHWLock->bSecureInst == VAL_FALSE) {
		MODULE_MFV_LOGD("[VCODEC_LOCKHW]  ENC Request IR by type 0x%x\n", prHWLock->eDriverType);
		if (request_irq(
			u4IrqId,
			(irq_handler_t)video_intr_dlr2,
			IRQF_TRIGGER_LOW,
			VCODEC_DEVNAME, NULL) < 0)	{
			MODULE_MFV_LOGD("[VCODEC_LOCKHW] ENC [MFV_DEBUG][ERROR] error to request enc irq\n");
		} else{
			MODULE_MFV_LOGD("[VCODEC_LOCKHW] ENC [MFV_DEBUG] success to request enc irq\n");
		}
	}
#else
	enable_irq(u4IrqId);
#endif

	MODULE_MFV_LOGD("venc_enableIRQ-\n");
	return 0;
}
static int vcodec_remove(struct platform_device *pDev)
{
	MODULE_MFV_LOGD("vcodec_remove\n");
	return 0;
}

#ifdef CONFIG_MTK_HIBERNATION
/* extern void mt_irq_set_sens(unsigned int irq, unsigned int sens); */
/* extern void mt_irq_set_polarity(unsigned int irq, unsigned int polarity); */
static int vcodec_pm_restore_noirq(struct device *device)
{
	/* vdec: IRQF_TRIGGER_LOW */
	mt_irq_set_sens(VDEC_IRQ_ID, MT_LEVEL_SENSITIVE);
	mt_irq_set_polarity(VDEC_IRQ_ID, MT_POLARITY_LOW);
	/* venc: IRQF_TRIGGER_LOW */
	mt_irq_set_sens(VENC_IRQ_ID, MT_LEVEL_SENSITIVE);
	mt_irq_set_polarity(VENC_IRQ_ID, MT_POLARITY_LOW);

	return 0;
}
#endif

static const struct of_device_id vcodec_of_match[] = {
	{ .compatible = "mediatek,vdec_gcon", },
	{/* sentinel */}
};

MODULE_DEVICE_TABLE(of, vcodec_of_match);

static struct platform_driver vcodec_driver = {
	.probe = vcodec_probe,
	.remove = vcodec_remove,
#if USE_WAKELOCK == 0
	.suspend = vcodec_suspend,
	.resume = vcodec_resume,
#endif
	.driver = {
		.name  = VCODEC_DEVNAME,
		.owner = THIS_MODULE,
		.of_match_table = vcodec_of_match,
	},
};

static int __init vcodec_driver_init(void)
{
	VAL_RESULT_T  eValHWLockRet;
	VAL_ULONG_T ulFlags, ulFlagsLockHW, ulFlagsISR;
	int error = 0;

	MODULE_MFV_LOGD("+vcodec_driver_init !!\n");

	mutex_lock(&DriverOpenCountLock);
	Driver_Open_Count = 0;
#if USE_WAKELOCK == 1
	wake_lock_init(&vcodec_wake_lock, WAKE_LOCK_SUSPEND, "vcodec_wake_lock");
	MODULE_MFV_PR_DEBUG("wake_lock_init(&vcodec_wake_lock, WAKE_LOCK_SUSPEND, \"vcodec_wake_lock\")");
	wake_lock_init(&vcodec_wake_lock2, WAKE_LOCK_SUSPEND, "vcodec_wake_lock2");
	MODULE_MFV_PR_DEBUG("wake_lock_init(&vcodec_wake_lock2, WAKE_LOCK_SUSPEND, \"vcodec_wake_lock2\")");
#endif
	mutex_unlock(&DriverOpenCountLock);

	mutex_lock(&LogCountLock);
	gu4LogCountUser = 0;
	gu4LogCount = 0;
	mutex_unlock(&LogCountLock);

	{
		struct device_node *node = NULL;

		node = of_find_compatible_node(NULL, NULL, "mediatek,venc");
		KVA_VENC_BASE = (VAL_ULONG_T)of_iomap(node, 0);
		VENC_IRQ_ID =  irq_of_parse_and_map(node, 0);
		KVA_VENC_IRQ_STATUS_ADDR =    KVA_VENC_BASE + 0x05C;
		KVA_VENC_IRQ_ACK_ADDR  = KVA_VENC_BASE + 0x060;
	}

	{
		struct device_node *node = NULL;

		node = of_find_compatible_node(NULL, NULL, "mediatek,vdec");
		KVA_VDEC_BASE = (VAL_ULONG_T)of_iomap(node, 0);
		VDEC_IRQ_ID =  irq_of_parse_and_map(node, 0);
		KVA_VDEC_MISC_BASE = KVA_VDEC_BASE + 0x0000;
		KVA_VDEC_VLD_BASE = KVA_VDEC_BASE + 0x1000;
	}
	{
		struct device_node *node = NULL;

		node = of_find_compatible_node(NULL, NULL, "mediatek,vdec_gcon");
		KVA_VDEC_GCON_BASE = (VAL_ULONG_T)of_iomap(node, 0);

		MODULE_MFV_LOGD("[VCODEC][DeviceTree] KVA_VENC_BASE(0x%lx), KVA_VDEC_BASE(0x%lx), KVA_VDEC_GCON_BASE(0x%lx)",
			 KVA_VENC_BASE, KVA_VDEC_BASE, KVA_VDEC_GCON_BASE);
		MODULE_MFV_LOGD("[VCODEC][DeviceTree] VDEC_IRQ_ID(%d), VENC_IRQ_ID(%d)",
			 VDEC_IRQ_ID, VENC_IRQ_ID);
	}

	/* KVA_VENC_IRQ_STATUS_ADDR =    (VAL_ULONG_T)ioremap(VENC_IRQ_STATUS_addr, 4); */
	/* KVA_VENC_IRQ_ACK_ADDR  = (VAL_ULONG_T)ioremap(VENC_IRQ_ACK_addr, 4); */

	spin_lock_irqsave(&LockDecHWCountLock, ulFlagsLockHW);
	gu4LockDecHWCount = 0;
	spin_unlock_irqrestore(&LockDecHWCountLock, ulFlagsLockHW);

	spin_lock_irqsave(&LockEncHWCountLock, ulFlagsLockHW);
	gu4LockEncHWCount = 0;
	spin_unlock_irqrestore(&LockEncHWCountLock, ulFlagsLockHW);

	spin_lock_irqsave(&DecISRCountLock, ulFlagsISR);
	gu4DecISRCount = 0;
	spin_unlock_irqrestore(&DecISRCountLock, ulFlagsISR);

	spin_lock_irqsave(&EncISRCountLock, ulFlagsISR);
	gu4EncISRCount = 0;
	spin_unlock_irqrestore(&EncISRCountLock, ulFlagsISR);

	mutex_lock(&VdecPWRLock);
	gu4VdecPWRCounter = 0;
	mutex_unlock(&VdecPWRLock);

	mutex_lock(&VencPWRLock);
	gu4VencPWRCounter = 0;
	mutex_unlock(&VencPWRLock);

	mutex_lock(&IsOpenedLock);
	if (bIsOpened == VAL_FALSE) {
		bIsOpened = VAL_TRUE;
		/* vcodec_probe(NULL); */
	}
	mutex_unlock(&IsOpenedLock);

	mutex_lock(&VdecHWLock);
	gu4VdecLockThreadId = 0;
	grVcodecDecHWLock.pvHandle = 0;
	grVcodecDecHWLock.eDriverType = VAL_DRIVER_TYPE_NONE;
	grVcodecDecHWLock.rLockedTime.u4Sec = 0;
	grVcodecDecHWLock.rLockedTime.u4uSec = 0;
	mutex_unlock(&VdecHWLock);

	mutex_lock(&VencHWLock);
	grVcodecEncHWLock.pvHandle = 0;
	grVcodecEncHWLock.eDriverType = VAL_DRIVER_TYPE_NONE;
	grVcodecEncHWLock.rLockedTime.u4Sec = 0;
	grVcodecEncHWLock.rLockedTime.u4uSec = 0;
	mutex_unlock(&VencHWLock);

	/* HWLockEvent part */
	mutex_lock(&DecHWLockEventTimeoutLock);
	DecHWLockEvent.pvHandle = "DECHWLOCK_EVENT";
	DecHWLockEvent.u4HandleSize = sizeof("DECHWLOCK_EVENT")+1;
	DecHWLockEvent.u4TimeoutMs = 1;
	mutex_unlock(&DecHWLockEventTimeoutLock);
	eValHWLockRet = eVideoCreateEvent(&DecHWLockEvent, sizeof(VAL_EVENT_T));
	if (eValHWLockRet != VAL_RESULT_NO_ERROR) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] create dec hwlock event error\n");
	}

	mutex_lock(&EncHWLockEventTimeoutLock);
	EncHWLockEvent.pvHandle = "ENCHWLOCK_EVENT";
	EncHWLockEvent.u4HandleSize = sizeof("ENCHWLOCK_EVENT")+1;
	EncHWLockEvent.u4TimeoutMs = 1;
	mutex_unlock(&EncHWLockEventTimeoutLock);
	eValHWLockRet = eVideoCreateEvent(&EncHWLockEvent, sizeof(VAL_EVENT_T));
	if (eValHWLockRet != VAL_RESULT_NO_ERROR) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] create enc hwlock event error\n");
	}

#ifdef VCODEC_DVFS_V2
	mutex_lock(&VdecDVFSLock);
	dec_hists = 0;
	dec_jobs = 0;
	mutex_unlock(&VdecDVFSLock);
#endif

	/* IsrEvent part */
	spin_lock_irqsave(&DecIsrLock, ulFlags);
	DecIsrEvent.pvHandle = "DECISR_EVENT";
	DecIsrEvent.u4HandleSize = sizeof("DECISR_EVENT")+1;
	DecIsrEvent.u4TimeoutMs = 1;
	spin_unlock_irqrestore(&DecIsrLock, ulFlags);
	eValHWLockRet = eVideoCreateEvent(&DecIsrEvent, sizeof(VAL_EVENT_T));
	if (eValHWLockRet != VAL_RESULT_NO_ERROR) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] create dec isr event error\n");
	}

	spin_lock_irqsave(&EncIsrLock, ulFlags);
	EncIsrEvent.pvHandle = "ENCISR_EVENT";
	EncIsrEvent.u4HandleSize = sizeof("ENCISR_EVENT")+1;
	EncIsrEvent.u4TimeoutMs = 1;
	spin_unlock_irqrestore(&EncIsrLock, ulFlags);
	eValHWLockRet = eVideoCreateEvent(&EncIsrEvent, sizeof(VAL_EVENT_T));
	if (eValHWLockRet != VAL_RESULT_NO_ERROR) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] create enc isr event error\n");
	}
#if USE_WAKELOCK == 0
	is_entering_suspend = 0;
#endif

	MODULE_MFV_LOGD("vcodec_driver_init Done\n");

#ifdef CONFIG_MTK_HIBERNATION
	register_swsusp_restore_noirq_func(ID_M_VCODEC, vcodec_pm_restore_noirq, NULL);
#endif

#ifdef VCODEC_DEBUG_SYS
	vcodec_debug_kobject = kobject_create_and_add("vcodec", NULL);

	if (!vcodec_debug_kobject) {
		pr_debug("Faile to create and add vcodec kobject");
		return -ENOMEM;
	}

	error = sysfs_create_file(vcodec_debug_kobject, &vcodec_debug_attr.attr);
	if (error) {
		pr_debug("Faile to create and add vcodec_debug file in /sys/vcodec/");
		return error;
	}
#endif

	return platform_driver_register(&vcodec_driver);
}

static void __exit vcodec_driver_exit(void)
{
	VAL_RESULT_T  eValHWLockRet;

	MODULE_MFV_LOGD("vcodec_driver_exit\n");
#if USE_WAKELOCK == 1
	mutex_lock(&DriverOpenCountLock);
	wake_lock_destroy(&vcodec_wake_lock);
	MODULE_MFV_PR_DEBUG("wake_lock_destroy(&vcodec_wake_lock)");
	wake_lock_destroy(&vcodec_wake_lock2);
	MODULE_MFV_PR_DEBUG("wake_lock_destroy(&vcodec_wake_lock2)");
	mutex_unlock(&DriverOpenCountLock);
#endif


	mutex_lock(&IsOpenedLock);
	if (bIsOpened == VAL_TRUE) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		bIsOpened = VAL_FALSE;
	}
	mutex_unlock(&IsOpenedLock);

	cdev_del(vcodec_cdev);
	unregister_chrdev_region(vcodec_devno, 1);
#ifdef CONFIG_PM
	cdev_del(vcodec_cdev2);
#endif
	/* [TODO] iounmap the following? */
#if 0
	iounmap((void *)KVA_VENC_IRQ_STATUS_ADDR);
	iounmap((void *)KVA_VENC_IRQ_ACK_ADDR);
#endif

	free_irq(VENC_IRQ_ID, NULL);
	free_irq(VDEC_IRQ_ID, NULL);

	/* MT6589_HWLockEvent part */
	eValHWLockRet = eVideoCloseEvent(&DecHWLockEvent, sizeof(VAL_EVENT_T));
	if (eValHWLockRet != VAL_RESULT_NO_ERROR) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] close dec hwlock event error\n");
	}

	eValHWLockRet = eVideoCloseEvent(&EncHWLockEvent, sizeof(VAL_EVENT_T));
	if (eValHWLockRet != VAL_RESULT_NO_ERROR) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] close enc hwlock event error\n");
	}

	/* MT6589_IsrEvent part */
	eValHWLockRet = eVideoCloseEvent(&DecIsrEvent, sizeof(VAL_EVENT_T));
	if (eValHWLockRet != VAL_RESULT_NO_ERROR) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] close dec isr event error\n");
	}

	eValHWLockRet = eVideoCloseEvent(&EncIsrEvent, sizeof(VAL_EVENT_T));
	if (eValHWLockRet != VAL_RESULT_NO_ERROR) {
		/* Add one line comment for avoid kernel coding style, WARNING:BRACES: */
		pr_debug("[VCODEC][ERROR] close enc isr event error\n");
	}

#ifdef CONFIG_MTK_HIBERNATION
	unregister_swsusp_restore_noirq_func(ID_M_VCODEC);
#endif

#ifdef VCODEC_DEBUG_SYS
	kobject_put(vcodec_debug_kobject);
#endif

	platform_driver_unregister(&vcodec_driver);
}

module_init(vcodec_driver_init);
module_exit(vcodec_driver_exit);
MODULE_AUTHOR("Legis, Lu <legis.lu@mediatek.com>");
MODULE_DESCRIPTION("Sylvia Vcodec Driver");
MODULE_LICENSE("GPL");
