#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/sched_clock.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <board-custom.h>


#include "mt_freqhopping.h"
#include "mt_fhreg.h"
#include "mach/mt_clkmgr.h"
#include "mach/mt_typedefs.h"
#include "mach/mt_gpio.h"
#include "mach/mt_gpufreq.h"
#include "mach/mt_cpufreq.h"
#include "mach/emi_bwl.h"
#include "mach/sync_write.h"
#include "mach/mt_sleep.h"

#include "mt_freqhopping_drv.h"
#include <mach/mt_clkmgr.h>
#include <linux/seq_file.h>


#ifdef CONFIG_OF
#include <linux/of_address.h>
static void __iomem *g_fhctl_base;
static void __iomem *g_apmixed_base;
static void __iomem *g_ddrphy_base;
#endif

/* masks */
#define MASK_FRDDSX_DYS         (0xFU<<20)
#define MASK_FRDDSX_DTS         (0xFU<<16)
#define FH_FHCTLX_SRHMODE       (0x1U<<5)
#define FH_SFSTRX_BP            (0x1U<<4)
#define FH_SFSTRX_EN            (0x1U<<2)
#define FH_FRDDSX_EN            (0x1U<<1)
#define FH_FHCTLX_EN            (0x1U<<0)
#define FH_FRDDSX_DNLMT         (0xFFU<<16)
#define FH_FRDDSX_UPLMT         (0xFFU)
#define FH_FHCTLX_PLL_TGL_ORG   (0x1U<<31)
#define FH_FHCTLX_PLL_ORG       (0xFFFFFU)
#define FH_FHCTLX_PAUSE         (0x1U<<31)
#define FH_FHCTLX_PRD           (0x1U<<30)
#define FH_SFSTRX_PRD           (0x1U<<29)
#define FH_FRDDSX_PRD           (0x1U<<28)
#define FH_FHCTLX_STATE         (0xFU<<24)
#define FH_FHCTLX_PLL_CHG       (0x1U<<21)
#define FH_FHCTLX_PLL_DDS       (0xFFFFFU)


#define USER_DEFINE_SETTING_ID	(1)

#define MASK21b (0x1FFFFF)
#define BIT32   (1U<<31)

static DEFINE_SPINLOCK(g_fh_lock);

#define PERCENT_TO_DDSLMT(dDS, pERCENT_M10) (((dDS * pERCENT_M10) >> 5) / 100)

static unsigned int g_initialize;

#ifndef PER_PROJECT_FH_SETTING

/* default VCO freq. */
#define ARMCA7PLL_DEF_FREQ      1599000
#define MEMPLL_DEF_FREQ          160000	/* /< It is 160Mbps provided from DRAM expert. */
#define MAINPLL_DEF_FREQ        1092000
#define MPLL_DEF_FREQ            208000	/* /< For MP32 used, freq is 208MHz */
#define MSDCPLL_DEF_FREQ        1600000
#define MMPLL_DEF_FREQ          1092000
#define VENCPLL_DEF_FREQ        1518002
#define TVDPLL_DEF_FREQ         1782000
#define VCODECPLL_DEF_FREQ      1976000

/* keep track the status of each PLL */
static fh_pll_t g_fh_pll[FH_PLL_NUM] = {
	{FH_FH_DISABLE, FH_PLL_ENABLE, 0, ARMCA7PLL_DEF_FREQ, 0},
	{FH_FH_DISABLE, FH_PLL_ENABLE, 0, MPLL_DEF_FREQ, 0},
	{FH_FH_DISABLE, FH_PLL_ENABLE, 0, MAINPLL_DEF_FREQ, 0},
	{FH_FH_ENABLE_SSC, FH_PLL_ENABLE, 0, MEMPLL_DEF_FREQ, 0},
	{FH_FH_DISABLE, FH_PLL_ENABLE, 0, MSDCPLL_DEF_FREQ, 0},
	{FH_FH_DISABLE, FH_PLL_ENABLE, 0, MMPLL_DEF_FREQ, 0},
	{FH_FH_DISABLE, FH_PLL_ENABLE, 0, VENCPLL_DEF_FREQ, 0},
	{FH_FH_DISABLE, FH_PLL_ENABLE, 0, TVDPLL_DEF_FREQ, 0},
	{FH_FH_DISABLE, FH_PLL_ENABLE, 0, VCODECPLL_DEF_FREQ, 0}
};

static const struct freqhopping_ssc ssc_armca7pll_setting[] = {
	{0, 0, 0, 0, 0, 0},	/* Means disable */
	{0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},	/* Means User-Define */
	{ARMCA7PLL_DEF_FREQ, 0, 9, 0, 8, 0xF6000},	/* 0 ~ -8% */
	{0, 0, 0, 0, 0, 0}	/* EOF */
};

static const struct freqhopping_ssc ssc_mpll_setting[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	/* {MPLL_DEF_FREQ ,0 ,9 ,0, 8, 0x1C000},//0 ~ -8% */
	{MPLL_DEF_FREQ, 0, 9, 0, 5, 0x1C000},	/* 0 ~ -5% */
	{0, 0, 0, 0, 0, 0}
};


static const struct freqhopping_ssc ssc_mainpll_setting[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	{MAINPLL_DEF_FREQ, 0, 9, 0, 8, 0xA8000},	/* 0 ~ -8% */
	{0, 0, 0, 0, 0, 0}
};

static const struct freqhopping_ssc ssc_mempll_setting[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	/* {MEMPLL_DEF_FREQ ,0 ,9 ,0, 8, 0x1C000},//0 ~ -8% */
	{MEMPLL_DEF_FREQ, 0, 9, 0, 2, 0x1C000},	/* 0 ~ -2% 0812 Requested by de-sense SA Leonard.Liang */
	{0, 0, 0, 0, 0, 0}
};


static const struct freqhopping_ssc ssc_msdcpll_setting[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	{MSDCPLL_DEF_FREQ, 0, 9, 0, 8, 0xF6276},	/* 0 ~ -8% */
	{0, 0, 0, 0, 0, 0}
};

static const struct freqhopping_ssc ssc_mmpll_setting[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	{MMPLL_DEF_FREQ, 0, 9, 0, 8, 0xD8000},	/* 0~-8% */
	{0, 0, 0, 0, 0, 0}
};

static const struct freqhopping_ssc ssc_vencpll_setting[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	{VENCPLL_DEF_FREQ, 0, 9, 0, 8, 0xE989E},	/* 0~-8% */
	{0, 0, 0, 0, 0, 0}
};

static const struct freqhopping_ssc ssc_tvdpll_setting[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	{TVDPLL_DEF_FREQ, 0, 9, 0, 8, 0x112276},	/* 0~-8% */
	{0, 0, 0, 0, 0, 0}
};

static const struct freqhopping_ssc ssc_vcodecpll_setting[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	{VCODECPLL_DEF_FREQ, 0, 9, 0, 8, 0x130000},	/* 0~-8% */
	{0, 0, 0, 0, 0, 0}
};

static const unsigned int g_default_freq[] = {
	ARMCA7PLL_DEF_FREQ, MPLL_DEF_FREQ, MAINPLL_DEF_FREQ,
	MEMPLL_DEF_FREQ, MSDCPLL_DEF_FREQ, MMPLL_DEF_FREQ,
	VENCPLL_DEF_FREQ, TVDPLL_DEF_FREQ, VCODECPLL_DEF_FREQ
};

static struct freqhopping_ssc mt_ssc_fhpll_userdefined[FH_PLL_NUM] = {
	{0, 1, 1, 2, 2, 0},	/* ARMCA7PLL */
	{0, 1, 1, 2, 2, 0},	/* MPLL5PLL */
	{0, 1, 1, 2, 2, 0},	/* MAINPLL */
	{0, 1, 1, 2, 2, 0},	/* MEMPLL */
	{0, 1, 1, 2, 2, 0},	/* MSDCPLL */
	{0, 1, 1, 2, 2, 0},	/* MMPLL */
	{0, 1, 1, 2, 2, 0},	/* VENCPLL */
	{0, 1, 1, 2, 2, 0},	/* TVDPLL */
	{0, 1, 1, 2, 2, 0}	/* VCODECPLL */
};

#else				/* PER_PROJECT_FH_SETTING */

PER_PROJECT_FH_SETTING
#endif				/* PER_PROJECT_FH_SETTING */
static const struct freqhopping_ssc *g_ssc_setting[] = {
	ssc_armca7pll_setting,
	ssc_mpll_setting,
	ssc_mainpll_setting,
	ssc_mempll_setting,
	ssc_msdcpll_setting,
	ssc_mmpll_setting,
	ssc_vencpll_setting,
	ssc_tvdpll_setting,
	ssc_vcodecpll_setting
};

static const unsigned int g_ssc_setting_size[] = {
	sizeof(ssc_armca7pll_setting) / sizeof(ssc_armca7pll_setting[0]),
	sizeof(ssc_mpll_setting) / sizeof(ssc_mpll_setting[0]),
	sizeof(ssc_mainpll_setting) / sizeof(ssc_mainpll_setting[0]),
	sizeof(ssc_mempll_setting) / sizeof(ssc_mempll_setting[0]),
	sizeof(ssc_msdcpll_setting) / sizeof(ssc_msdcpll_setting[0]),
	sizeof(ssc_mmpll_setting) / sizeof(ssc_mmpll_setting[0]),
	sizeof(ssc_vencpll_setting) / sizeof(ssc_vencpll_setting[0]),
	sizeof(ssc_tvdpll_setting) / sizeof(ssc_tvdpll_setting[0]),
	sizeof(ssc_vcodecpll_setting) / sizeof(ssc_vcodecpll_setting[0])
};


#ifdef CONFIG_OF
static unsigned long g_reg_dds[FH_PLL_NUM];
static unsigned long g_reg_cfg[FH_PLL_NUM];
static unsigned long g_reg_updnlmt[FH_PLL_NUM];
static unsigned long g_reg_mon[FH_PLL_NUM];
static unsigned long g_reg_dvfs[FH_PLL_NUM];
static unsigned long g_reg_pll_con1[FH_PLL_NUM];
#else

static const unsigned long g_reg_pll_con1[] = {
	REG_ARMCA7PLL_CON1, REG_MPLL_CON1, REG_MAINPLL_CON1,
	REG_MEMPLL_CON1, REG_MSDCPLL_CON1, REG_MMPLL_CON1,
	REG_VENCPLL_CON1, REG_TVDPLL_CON1, REG_VCODECPLL_CON1
};

static const unsigned long g_reg_dds[] = {
	REG_FHCTL0_DDS, REG_FHCTL1_DDS, REG_FHCTL2_DDS,
	REG_FHCTL3_DDS, REG_FHCTL4_DDS, REG_FHCTL5_DDS,
	REG_FHCTL6_DDS, REG_FHCTL7_DDS, REG_FHCTL8_DDS
};

static const unsigned long g_reg_cfg[] = {
	REG_FHCTL0_CFG, REG_FHCTL1_CFG, REG_FHCTL2_CFG,
	REG_FHCTL3_CFG, REG_FHCTL4_CFG, REG_FHCTL5_CFG,
	REG_FHCTL6_CFG, REG_FHCTL7_CFG, REG_FHCTL8_CFG
};

static const unsigned long g_reg_updnlmt[] = {
	REG_FHCTL0_UPDNLMT, REG_FHCTL1_UPDNLMT, REG_FHCTL2_UPDNLMT,
	REG_FHCTL3_UPDNLMT, REG_FHCTL4_UPDNLMT, REG_FHCTL5_UPDNLMT,
	REG_FHCTL6_UPDNLMT, REG_FHCTL7_UPDNLMT, REG_FHCTL8_UPDNLMT
};

static const unsigned long g_reg_mon[] = {
	REG_FHCTL0_MON, REG_FHCTL1_MON, REG_FHCTL2_MON,
	REG_FHCTL3_MON, REG_FHCTL4_MON, REG_FHCTL5_MON,
	REG_FHCTL6_MON, REG_FHCTL7_MON, REG_FHCTL8_MON
};

static const unsigned long g_reg_dvfs[] = {
	REG_FHCTL0_DVFS, REG_FHCTL1_DVFS, REG_FHCTL2_DVFS,
	REG_FHCTL3_DVFS, REG_FHCTL4_DVFS, REG_FHCTL5_DVFS,
	REG_FHCTL6_DVFS, REG_FHCTL7_DVFS, REG_FHCTL8_DVFS
};
#endif				/* CONFIG_OF */

#define VALIDATE_PLLID(id)BUG_ON(id >= FH_PLL_NUM)

/* #define ENABLE_DVT_LTE_SIDEBAND_SIGNAL_TESTCASE */

/* caller: clk mgr */
static void mt_fh_hal_default_conf(void)
{
	FH_MSG_DEBUG("%s", __func__);

#if 0
	/* freqhopping_config(FH_ARMCA7_PLLID, g_default_freq[FH_ARMCA7_PLLID], false); */
	/* freqhopping_config(FH_M_PLLID, g_default_freq[FH_M_PLLID], true); */
	/* freqhopping_config(FH_MAIN_PLLID, g_default_freq[FH_MAIN_PLLID], true); */
	/* freqhopping_config(FH_MEM_PLLID, g_default_freq[FH_MEM_PLLID], true); */
	/* freqhopping_config(FH_MSDC_PLLID, g_default_freq[FH_MSDC_PLLID], true); */
	/* freqhopping_config(FH_MM_PLLID, g_default_freq[FH_MM_PLLID], true); */
	/* freqhopping_config(FH_VENC_PLLID, g_default_freq[FH_VENC_PLLID], true); */
	/* freqhopping_config(FH_TVD_PLLID, g_default_freq[FH_TVD_PLLID], true); */
	/* freqhopping_config(FH_VCODEC_PLLID, g_default_freq[FH_VCODEC_PLLID], true); */
#endif

#ifdef ENABLE_DVT_LTE_SIDEBAND_SIGNAL_TESTCASE
	fh_set_field(REG_FHCTL1_DDS, (0x1FFFFFU << 0), 0X100000);	/* /< Set default MPLL DDS */
	fh_set_field(REG_FHCTL1_DDS, (0x1U << 32), 1);

	fh_set_field(REG_FHCTL_HP_EN, (0x1U << 31), 1);	/* /< Enable LTE Sideband signal */
	fh_set_field(REG_FHCTL_HP_EN, (0x1U << 1), 0x1);	/* /< MPLL */

	fh_set_field(REG_FHCTL1_CFG, (0x1U << 0), 1);	/* /< Enable */
	fh_set_field(REG_FHCTL1_CFG, (0x1U << 3), 1);	/* /< DYSSC Enable */

	/* /< Set FHCTL_DSSC_CFG(0x1000CF14), Bit3 is RF3(LTE) SSC control. Clear to 0 is enable. */
	fh_set_field(REG_FHCTL_DSSC_CFG, (0x1U << 3), 0);
	fh_set_field(REG_FHCTL_DSSC_CFG, (0x1U << 19), 0);	/* /< RF(3) LTE BAN control */

	/* fh_set_field(REG_FHCTL_DSSC3_CON, (0x1U<<1), 1); */
#endif
}

static void fh_switch2fhctl(enum FH_PLL_ID pll_id, int i_control)
{
	unsigned int mask = 0;

	VALIDATE_PLLID(pll_id);

	mask = 0x1U << pll_id;

	/* FIXME: clock should be turned on/off at entry functions */
	/* Turn on clock */
	/* if (i_control == 1) */
	/* fh_set_field(REG_FHCTL_CLK_CON, mask, i_control); */

	/* Release software reset */
	/* fh_set_field(REG_FHCTL_RST_CON, mask, 0); */

	/* Switch to FHCTL_CORE controller */
	fh_set_field(REG_FHCTL_HP_EN, mask, i_control);

	/* Turn off clock */
	/* if (i_control == 0) */
	/* fh_set_field(REG_FHCTL_CLK_CON, mask, i_control); */

	return;
}

static void fh_sync_ncpo_to_fhctl_dds(enum FH_PLL_ID pll_id)
{
	unsigned long reg_src = 0;
	unsigned long reg_dst = 0;

	VALIDATE_PLLID(pll_id);

	reg_src = g_reg_pll_con1[pll_id];
	reg_dst = g_reg_dds[pll_id];

	if (pll_id == FH_MEM_PLLID) {
		/* K2 mempll con1 field definition is not same as other. [30:10] */
		fh_write32(reg_dst, ((fh_read32(reg_src) >> 10) & MASK21b) | BIT32);
	} else {
		fh_write32(reg_dst, (fh_read32(reg_src) & MASK21b) | BIT32);
	}
	return;
}

static void __enable_ssc(unsigned int pll_id, const struct freqhopping_ssc *setting)
{
	unsigned long flags = 0;
	const unsigned long reg_cfg = g_reg_cfg[pll_id];
	const unsigned long reg_updnlmt = g_reg_updnlmt[pll_id];
	const unsigned long reg_dds = g_reg_dds[pll_id];

	FH_MSG_DEBUG("%s: %x~%x df:%d dt:%d dds:%x",
		     __func__, setting->lowbnd, setting->upbnd, setting->df, setting->dt,
		     setting->dds);

	mb();

	g_fh_pll[pll_id].fh_status = FH_FH_ENABLE_SSC;

	local_irq_save(flags);

	/* Set the relative parameter registers (dt/df/upbnd/downbnd) */
	fh_set_field(reg_cfg, MASK_FRDDSX_DYS, setting->df);
	fh_set_field(reg_cfg, MASK_FRDDSX_DTS, setting->dt);

	fh_sync_ncpo_to_fhctl_dds(pll_id);

	/* TODO: Not setting upper due to they are all 0? */
	fh_write32(reg_updnlmt,
		   (PERCENT_TO_DDSLMT((fh_read32(reg_dds) & MASK21b), setting->lowbnd) << 16));

	/* Switch to FHCTL */
	fh_switch2fhctl(pll_id, 1);
	mb();

	/* Enable SSC */
	fh_set_field(reg_cfg, FH_FRDDSX_EN, 1);
	/* Enable Hopping control */
	fh_set_field(reg_cfg, FH_FHCTLX_EN, 1);

	local_irq_restore(flags);

	return;
}

static void __disable_ssc(unsigned int pll_id, const struct freqhopping_ssc *ssc_setting)
{
	unsigned long flags = 0;
	unsigned long reg_cfg = g_reg_cfg[pll_id];

	FH_MSG_DEBUG("Calling %s", __func__);

	local_irq_save(flags);

	/* Set the relative registers */
	fh_set_field(reg_cfg, FH_FRDDSX_EN, 0);
	fh_set_field(reg_cfg, FH_FHCTLX_EN, 0);
	mb();
	fh_switch2fhctl(pll_id, 0);
	g_fh_pll[pll_id].fh_status = FH_FH_DISABLE;
	local_irq_restore(flags);
	mb();

	return;
}

/* freq is in KHz, return at which number of entry in mt_ssc_xxx_setting[] */
static noinline int __freq_to_index(enum FH_PLL_ID pll_id, int freq)
{
	unsigned int retVal = 0;
	unsigned int i = 2;	/* 0 is disable, 1 is user defines, so start from 2 */
	const unsigned int size = g_ssc_setting_size[pll_id];

	while (i < size) {
		if (freq == g_ssc_setting[pll_id][i].freq) {
			retVal = i;
			break;
		}
		++i;
	}

	return retVal;
}

static int __freqhopping_ctrl(struct freqhopping_ioctl *fh_ctl, bool enable)
{
	const struct freqhopping_ssc *pSSC_setting = NULL;
	unsigned int ssc_setting_id = 0;
	int retVal = 1;
	fh_pll_t *pfh_pll = NULL;

	FH_MSG_DEBUG("%s for pll %d", __func__, fh_ctl->pll_id);

	/* Check the out of range of frequency hopping PLL ID */
	VALIDATE_PLLID(fh_ctl->pll_id);

	pfh_pll = &g_fh_pll[fh_ctl->pll_id];

	pfh_pll->curr_freq = g_default_freq[fh_ctl->pll_id];

	if ((enable == true) && (pfh_pll->fh_status == FH_FH_ENABLE_SSC)) {
		__disable_ssc(fh_ctl->pll_id, pSSC_setting);
	} else if ((enable == false) && (pfh_pll->fh_status == FH_FH_DISABLE)) {
		retVal = 0;
		goto Exit;
	}
	/* enable freq. hopping @ fh_ctl->pll_id */
	if (enable == true) {
		if (pfh_pll->pll_status == FH_PLL_DISABLE) {
			pfh_pll->fh_status = FH_FH_ENABLE_SSC;
			retVal = 0;
			goto Exit;
		} else {
			if (pfh_pll->user_defined == true) {
				FH_MSG_DEBUG("Apply user defined setting");

				pSSC_setting = &mt_ssc_fhpll_userdefined[fh_ctl->pll_id];
				pfh_pll->setting_id = USER_DEFINE_SETTING_ID;
			} else {
				if (pfh_pll->curr_freq != 0) {
					ssc_setting_id = pfh_pll->setting_id =
					    __freq_to_index(fh_ctl->pll_id, pfh_pll->curr_freq);
				} else {
					ssc_setting_id = 0;
				}

				if (ssc_setting_id == 0) {
					FH_MSG_DEBUG("!!! No corresponding setting found !!!");

					/* just disable FH & exit */
					__disable_ssc(fh_ctl->pll_id, pSSC_setting);
					goto Exit;
				}

				pSSC_setting = &g_ssc_setting[fh_ctl->pll_id][ssc_setting_id];
			}	/* user defined */

			if (pSSC_setting == NULL) {
				FH_MSG_DEBUG("SSC_setting is NULL!");

				/* disable FH & exit */
				__disable_ssc(fh_ctl->pll_id, pSSC_setting);
				goto Exit;
			}

			__enable_ssc(fh_ctl->pll_id, pSSC_setting);
			retVal = 0;
		}
	} else {		/* disable req. hopping @ fh_ctl->pll_id */
		__disable_ssc(fh_ctl->pll_id, pSSC_setting);
		retVal = 0;
	}

Exit:
	return retVal;
}

static void wait_dds_stable(unsigned int target_dds, unsigned long reg_mon, unsigned int wait_count)
{
	unsigned int fh_dds = 0;
	unsigned int i = 0;

	fh_dds = DRV_Reg32(reg_mon) & MASK21b;
	while ((target_dds != fh_dds) && (i < wait_count)) {
		udelay(10);
#if 0
		if (unlikely(i > 100)) {
			BUG_ON(1);
			break;
		}
#endif
		fh_dds = (DRV_Reg32(reg_mon)) & MASK21b;
		++i;
	}
	if (i > 100)
		FH_MSG("target_dds = %d, fh_dds = %d, i = %d", target_dds, fh_dds, i);

}

static int mt_fh_hal_dvfs(enum FH_PLL_ID pll_id, unsigned int dds_value)
{
	unsigned long flags = 0;

	FH_MSG_DEBUG("%s for pll %d:", __func__, pll_id);

	VALIDATE_PLLID(pll_id);

	local_irq_save(flags);

	/* 1. sync ncpo to DDS of FHCTL */
	fh_sync_ncpo_to_fhctl_dds(pll_id);

	/* FH_MSG("1. sync ncpo to DDS of FHCTL"); */
	FH_MSG_DEBUG("FHCTL%d_DDS: 0x%08x", pll_id, (fh_read32(g_reg_dds[pll_id]) & MASK21b));

	/* 2. enable DVFS and Hopping control */
	{
		unsigned long reg_cfg = g_reg_cfg[pll_id];

		fh_set_field(reg_cfg, FH_SFSTRX_EN, 1);	/* enable dvfs mode */
		fh_set_field(reg_cfg, FH_FHCTLX_EN, 1);	/* enable hopping control */
	}

	/* for slope setting. */
	/* TODO: Does this need to be changed? */
	fh_write32(REG_FHCTL_SLOPE0, 0x6003c97);

	/* FH_MSG("2. enable DVFS and Hopping control"); */

	/* 3. switch to hopping control */
	fh_switch2fhctl(pll_id, 1);
	mb();

	/* FH_MSG("3. switch to hopping control"); */

	/* 4. set DFS DDS */
	{
		unsigned long dvfs_req = g_reg_dvfs[pll_id];
		fh_write32(dvfs_req, (dds_value) | (BIT32));	/* set dds */

		/* FH_MSG("4. set DFS DDS"); */
		FH_MSG_DEBUG("FHCTL%d_DDS: 0x%08x", pll_id, (fh_read32(dvfs_req) & MASK21b));
		FH_MSG_DEBUG("FHCTL%d_DVFS: 0x%08x", pll_id, (fh_read32(dvfs_req) & MASK21b));
	}

	/* 4.1 ensure jump to target DDS */
	wait_dds_stable(dds_value, g_reg_mon[pll_id], 100);
	/* FH_MSG("4.1 ensure jump to target DDS"); */

	/* 5. write back to ncpo */
	/* FH_MSG("5. write back to ncpo"); */
	{
		unsigned long reg_dvfs = 0;
		unsigned long reg_pll_con1 = 0;

		if (pll_id == FH_MEM_PLLID) {
			/* K2 MEMPLL CON1 reg is not same as other CON1. */
			reg_pll_con1 = g_reg_pll_con1[pll_id];
			reg_dvfs = g_reg_dvfs[pll_id];
			FH_MSG_DEBUG("MEMPLL_CON1: 0x%08x", (fh_read32(reg_pll_con1)));

			/* /< left shift 10bit to [30:20] */
			fh_write32(reg_pll_con1, ((fh_read32(g_reg_mon[pll_id]) & MASK21b) << 10)
				   | (fh_read32(reg_pll_con1) & 0x80000000) | (BIT32));
			FH_MSG_DEBUG("MEMPLL_CON1: 0x%08x", (fh_read32(reg_pll_con1)));
		} else {
			reg_pll_con1 = g_reg_pll_con1[pll_id];
			reg_dvfs = g_reg_dvfs[pll_id];
			FH_MSG_DEBUG("PLL_CON1: 0x%08x", (fh_read32(reg_pll_con1) & MASK21b));

			fh_write32(reg_pll_con1, (fh_read32(g_reg_mon[pll_id]) & MASK21b)
				   | (fh_read32(reg_pll_con1) & 0xFFE00000) | (BIT32));
			FH_MSG_DEBUG("PLL_CON1: 0x%08x", (fh_read32(reg_pll_con1) & MASK21b));
		}
	}

	/* 6. switch to register control */
	fh_switch2fhctl(pll_id, 0);
	mb();

	/* FH_MSG("6. switch to register control"); */

	local_irq_restore(flags);
	return 0;
}

/* armpll dfs mdoe */
static int mt_fh_hal_dfs_armpll(unsigned int pll, unsigned int dds)
{
	unsigned long flags = 0;
	unsigned long reg_cfg = 0;

	if (g_initialize == 0) {
		FH_MSG("(Warning) %s FHCTL isn't ready.", __func__);
		return -1;
	}
	/* FH_MSG("%s for pll %d dds %d", __func__, pll, dds); */

	switch (pll) {
	case FH_ARMCA7_PLLID:
		reg_cfg = g_reg_cfg[pll];
		/* FH_MSG("(PLL_CON1): 0x%x",(fh_read32(g_reg_pll_con1[pll])&MASK21b)); */
		break;
	default:
		BUG_ON(1);
		return 1;
	};

	/* TODO: provelock issue spin_lock(&g_fh_lock); */
	spin_lock_irqsave(&g_fh_lock, flags);

	fh_set_field(reg_cfg, FH_FRDDSX_EN, 0);	/* disable SSC mode */
	fh_set_field(reg_cfg, FH_SFSTRX_EN, 0);	/* disable dvfs mode */
	fh_set_field(reg_cfg, FH_FHCTLX_EN, 0);	/* disable hopping control */

	mt_fh_hal_dvfs(pll, dds);

	fh_set_field(reg_cfg, FH_FRDDSX_EN, 0);	/* disable SSC mode */
	fh_set_field(reg_cfg, FH_SFSTRX_EN, 0);	/* disable dvfs mode */
	fh_set_field(reg_cfg, FH_FHCTLX_EN, 0);	/* disable hopping control */

	spin_unlock_irqrestore(&g_fh_lock, flags);

	return 0;
}

static int mt_fh_hal_dfs_mmpll(unsigned int target_dds)
{				/* mmpll dfs mode */
	unsigned long flags = 0;
	const unsigned int pll_id = FH_MM_PLLID;
	const unsigned long reg_cfg = g_reg_cfg[pll_id];

	if (g_initialize == 0) {
		FH_MSG("(Warning) %s FHCTL isn't ready. ", __func__);
		return -1;
	}

	FH_MSG_DEBUG("%s, current dds(MMPLL_CON1): 0x%x, target dds %d",
	       __func__, (fh_read32(g_reg_pll_con1[pll_id]) & MASK21b), target_dds);

	spin_lock_irqsave(&g_fh_lock, flags);

	if (g_fh_pll[pll_id].fh_status == FH_FH_ENABLE_SSC) {
		unsigned int pll_dds = 0;
		unsigned int fh_dds = 0;

		/* only when SSC is enable, turn off MEMPLL hopping */
		fh_set_field(reg_cfg, FH_FRDDSX_EN, 0);	/* disable SSC mode */
		fh_set_field(reg_cfg, FH_SFSTRX_EN, 0);	/* disable dvfs mode */
		fh_set_field(reg_cfg, FH_FHCTLX_EN, 0);	/* disable hopping control */

		pll_dds = (DRV_Reg32(g_reg_dds[pll_id])) & MASK21b;
		fh_dds = (DRV_Reg32(g_reg_mon[pll_id])) & MASK21b;

		FH_MSG_DEBUG(">p:f< %x:%x", pll_dds, fh_dds);

		wait_dds_stable(pll_dds, g_reg_mon[pll_id], 100);
	}
#if 0
	target_dds = (target_freq / 1000) * 8192 / 13;
#endif

	FH_MSG_DEBUG("target dds: 0x%x", target_dds);
	mt_fh_hal_dvfs(pll_id, target_dds);

	if (g_fh_pll[pll_id].fh_status == FH_FH_ENABLE_SSC) {
		const struct freqhopping_ssc *p_setting = &ssc_mmpll_setting[2];

		fh_set_field(reg_cfg, FH_FRDDSX_EN, 0);	/* disable SSC mode */
		fh_set_field(reg_cfg, FH_SFSTRX_EN, 0);	/* disable dvfs mode */
		fh_set_field(reg_cfg, FH_FHCTLX_EN, 0);	/* disable hopping control */

		fh_sync_ncpo_to_fhctl_dds(pll_id);

		FH_MSG_DEBUG("Enable mmpll SSC mode");
		FH_MSG_DEBUG("DDS: 0x%08x", (fh_read32(g_reg_dds[pll_id]) & MASK21b));

		fh_set_field(reg_cfg, MASK_FRDDSX_DYS, p_setting->df);
		fh_set_field(reg_cfg, MASK_FRDDSX_DTS, p_setting->dt);

		fh_write32(g_reg_updnlmt[pll_id],
			   (PERCENT_TO_DDSLMT
			    ((fh_read32(g_reg_dds[pll_id]) & MASK21b), p_setting->lowbnd) << 16));
		FH_MSG_DEBUG("UPDNLMT: 0x%08x", fh_read32(g_reg_updnlmt[pll_id]));

		fh_switch2fhctl(pll_id, 1);

		fh_set_field(reg_cfg, FH_FRDDSX_EN, 1);	/* enable SSC mode */
		fh_set_field(reg_cfg, FH_FHCTLX_EN, 1);	/* enable hopping control */

		FH_MSG_DEBUG("CFG: 0x%08x", fh_read32(reg_cfg));

	}
	spin_unlock_irqrestore(&g_fh_lock, flags);

	return 0;
}

static int mt_fh_hal_dfs_vencpll(unsigned int target_freq)
{
	unsigned long flags = 0;
	unsigned int target_dds = 0;
	const unsigned int pll_id = FH_VENC_PLLID;
	const unsigned long reg_cfg = g_reg_cfg[pll_id];

	if (g_initialize == 0) {
		FH_MSG("(Warning) %s FHCTL isn't ready. ", __func__);
		return -1;
	}

	FH_MSG_DEBUG("%s current dds(VENCPLL_CON1): 0x%x", __func__,
		     (fh_read32(g_reg_pll_con1[pll_id]) & MASK21b));

	/* TODO: provelock issue spin_lock(&g_fh_lock); */
	spin_lock_irqsave(&g_fh_lock, flags);

	if (g_fh_pll[pll_id].fh_status == FH_FH_ENABLE_SSC) {
		unsigned int fh_dds = 0;
		unsigned int pll_dds = 0;

		fh_set_field(reg_cfg, FH_FRDDSX_EN, 0);	/* disable SSC mode */
		fh_set_field(reg_cfg, FH_SFSTRX_EN, 0);	/* disable dvfs mode */
		fh_set_field(reg_cfg, FH_FHCTLX_EN, 0);	/* disable hopping control */

		pll_dds = (DRV_Reg32(g_reg_dds[pll_id])) & MASK21b;
		fh_dds = (DRV_Reg32(g_reg_mon[pll_id])) & MASK21b;

		FH_MSG_DEBUG(">p:f< %x:%x", pll_dds, fh_dds);

		wait_dds_stable(pll_dds, g_reg_mon[pll_id], 100);
	}

	switch (target_freq) {
#if 0
	case VENCPLL_TARGETVCO_1:
		target_dds = VENCPLL_TARGETVCO_1_DDS;
		break;
	case VENCPLL_TARGETVCO_2:
		target_dds = VENCPLL_TARGETVCO_2_DDS;
		break;
#endif
	default:
		FH_BUG_ON(1);
		return 0;
		/* break; */
	};

	FH_MSG_DEBUG("target dds: 0x%x", target_dds);

	mt_fh_hal_dvfs(pll_id, target_dds);

	if (g_fh_pll[pll_id].fh_status == FH_FH_ENABLE_SSC) {
		const struct freqhopping_ssc *p_setting = &ssc_vencpll_setting[2];

		fh_set_field(reg_cfg, FH_FRDDSX_EN, 0);	/* disable SSC mode */
		fh_set_field(reg_cfg, FH_SFSTRX_EN, 0);	/* disable dvfs mode */
		fh_set_field(reg_cfg, FH_FHCTLX_EN, 0);	/* disable hopping control */

		fh_sync_ncpo_to_fhctl_dds(pll_id);

		FH_MSG_DEBUG("Enable vencpll SSC mode");
		FH_MSG_DEBUG("DDS: 0x%08x", (fh_read32(g_reg_dds[pll_id]) & MASK21b));

		fh_set_field(reg_cfg, MASK_FRDDSX_DYS, p_setting->df);
		fh_set_field(reg_cfg, MASK_FRDDSX_DTS, p_setting->dt);

		fh_write32(g_reg_updnlmt[pll_id],
			   (PERCENT_TO_DDSLMT
			    ((fh_read32(g_reg_dds[pll_id]) & MASK21b), p_setting->lowbnd) << 16));
		FH_MSG_DEBUG("UPDNLMT: 0x%08x", fh_read32(g_reg_updnlmt[pll_id]));

		fh_switch2fhctl(pll_id, 1);

		fh_set_field(reg_cfg, FH_FRDDSX_EN, 1);	/* enable SSC mode */
		fh_set_field(reg_cfg, FH_FHCTLX_EN, 1);	/* enable hopping control */

		FH_MSG_DEBUG("CFG: 0x%08x", fh_read32(reg_cfg));
	}
	spin_unlock_irqrestore(&g_fh_lock, flags);

	return 0;
}

static int mt_fh_hal_l2h_dvfs_mempll(void)
{
	FH_BUG_ON(1);
	return 0;
}

static int mt_fh_hal_h2l_dvfs_mempll(void)
{
	FH_BUG_ON(1);
	return 0;
}

static int mt_fh_hal_dram_overclock(int clk)
{
	FH_BUG_ON(1);
	return 0;
}

static int mt_fh_hal_get_dramc(void)
{
	FH_BUG_ON(1);
	return 0;
}

static void mt_fh_hal_popod_save(void)
{
	const unsigned int pll_id = FH_MAIN_PLLID;

	FH_MSG_DEBUG("EN: %s", __func__);

	/* disable maipll SSC mode */
	if (g_fh_pll[pll_id].fh_status == FH_FH_ENABLE_SSC) {
		unsigned int fh_dds = 0;
		unsigned int pll_dds = 0;
		const unsigned long reg_cfg = g_reg_cfg[pll_id];

		/* only when SSC is enable, turn off MAINPLL hopping */
		fh_set_field(reg_cfg, FH_FRDDSX_EN, 0);	/* disable SSC mode */
		fh_set_field(reg_cfg, FH_SFSTRX_EN, 0);	/* disable dvfs mode */
		fh_set_field(reg_cfg, FH_FHCTLX_EN, 0);	/* disable hopping control */

		pll_dds = (DRV_Reg32(g_reg_dds[pll_id])) & MASK21b;
		fh_dds = (DRV_Reg32(g_reg_mon[pll_id])) & MASK21b;

		FH_MSG_DEBUG("Org pll_dds:%x fh_dds:%x", pll_dds, fh_dds);

		wait_dds_stable(pll_dds, g_reg_mon[pll_id], 100);


		/* write back to ncpo, only for MAINPLL. Don't need to add MEMPLL handle. */
		fh_write32(g_reg_pll_con1[pll_id],
			   (fh_read32(g_reg_dds[pll_id]) & MASK21b) | (fh_read32(MAINPLL_CON1) &
								       0xFFE00000) | (BIT32));
		FH_MSG_DEBUG("MAINPLL_CON1: 0x%08x", (fh_read32(g_reg_pll_con1[pll_id]) & MASK21b));

		/* switch to register control */
		fh_switch2fhctl(pll_id, 0);

		mb();
	}
}

static void mt_fh_hal_popod_restore(void)
{
	const unsigned int pll_id = FH_MAIN_PLLID;

	FH_MSG_DEBUG("EN: %s", __func__);

	/* enable maipll SSC mode */
	if (g_fh_pll[pll_id].fh_status == FH_FH_ENABLE_SSC) {
		const struct freqhopping_ssc *p_setting = &ssc_mainpll_setting[2];
		const unsigned long reg_cfg = g_reg_cfg[pll_id];

		fh_set_field(reg_cfg, FH_FRDDSX_EN, 0);	/* disable SSC mode */
		fh_set_field(reg_cfg, FH_SFSTRX_EN, 0);	/* disable dvfs mode */
		fh_set_field(reg_cfg, FH_FHCTLX_EN, 0);	/* disable hopping control */

		fh_sync_ncpo_to_fhctl_dds(pll_id);

		FH_MSG_DEBUG("Enable mainpll SSC mode");
		FH_MSG_DEBUG("sync ncpo to DDS of FHCTL");
		FH_MSG_DEBUG("FHCTL1_DDS: 0x%08x", (fh_read32(g_reg_dds[pll_id]) & MASK21b));

		fh_set_field(reg_cfg, MASK_FRDDSX_DYS, p_setting->df);
		fh_set_field(reg_cfg, MASK_FRDDSX_DTS, p_setting->dt);

		fh_write32(g_reg_updnlmt[pll_id],
			   (PERCENT_TO_DDSLMT
			    ((fh_read32(g_reg_dds[pll_id]) & MASK21b), p_setting->lowbnd) << 16));
		FH_MSG_DEBUG("REG_FHCTL2_UPDNLMT: 0x%08x", fh_read32(g_reg_updnlmt[pll_id]));

		fh_switch2fhctl(pll_id, 1);

		fh_set_field(reg_cfg, FH_FRDDSX_EN, 1);	/* enable SSC mode */
		fh_set_field(reg_cfg, FH_FHCTLX_EN, 1);	/* enable hopping control */

		FH_MSG_DEBUG("REG_FHCTL2_CFG: 0x%08x", fh_read32(reg_cfg));
	}
}

static int fh_dramc_proc_read(struct seq_file *m, void *v)
{
	return 0;
}

static int fh_dramc_proc_write(struct file *file, const char *buffer, unsigned long count,
			       void *data)
{
	return 0;
}

static int fh_dvfs_proc_read(struct seq_file *m, void *v)
{
	int i = 0;

	FH_MSG_DEBUG("EN: %s", __func__);

	seq_puts(m, "DVFS:\r\n");
	seq_puts(m, "CFG: 0x3 is SSC mode;  0x5 is DVFS mode \r\n");
	for (i = 0; i < FH_PLL_NUM; ++i) {
		seq_printf(m, "FHCTL%d:   CFG:0x%08x    DVFS:0x%08x\r\n",
			   i, DRV_Reg32(g_reg_cfg[i]), DRV_Reg32(g_reg_dvfs[i]));
	}
	return 0;
}

static int fh_dvfs_proc_write(struct file *file, const char *buffer, unsigned long count,
			      void *data)
{
	unsigned int p1, p2, p3, p4, p5;

	p1 = p2 = p3 = p4 = p5 = 0;

	FH_MSG_DEBUG("EN: %s", __func__);

	if (count == 0)
		return -1;

	FH_MSG_DEBUG("EN: p1=%d p2=%d p3=%d", p1, p2, p3);

	switch (p1) {
	case FH_ARMCA7_PLLID:
		mt_fh_hal_dfs_armpll(p2, p3);
		FH_MSG_DEBUG("ARMCA7PLL DVFS completed\n");
		break;
	case FH_MM_PLLID:
		mt_fh_hal_dfs_mmpll(p3);
		FH_MSG_DEBUG("MMPLL DVFS completed\n");
		break;
	case FH_VENC_PLLID:
		mt_fh_hal_dfs_vencpll(p3);
		FH_MSG_DEBUG("VENCPLL DVFS completed\n");
		break;
	case 4370:
		{
			unsigned long reg_cfg = 0;

			VALIDATE_PLLID(p2);

			reg_cfg = g_reg_cfg[p2];

			/* TODO: Find out who use this case */
			FH_MSG_DEBUG("pllid=%d dt=%d df=%d lowbnd=%d", p2, p3, p4, p5);
			fh_set_field(reg_cfg, FH_FRDDSX_EN, 0);	/* disable SSC mode */
			fh_set_field(reg_cfg, FH_SFSTRX_EN, 0);	/* disable dvfs mode */
			fh_set_field(reg_cfg, FH_FHCTLX_EN, 0);	/* disable hopping control */

			fh_sync_ncpo_to_fhctl_dds(p2);

			FH_MSG_DEBUG("Enable FHCTL%d SSC mode", p2);
			FH_MSG_DEBUG("DDS: 0x%08x", (fh_read32(reg_cfg) & MASK21b));

			fh_set_field(reg_cfg, MASK_FRDDSX_DYS, p4);
			fh_set_field(reg_cfg, MASK_FRDDSX_DTS, p3);

			fh_write32(g_reg_updnlmt[p2],
				   (PERCENT_TO_DDSLMT((fh_read32(reg_cfg) & MASK21b), p5) << 16));
			FH_MSG_DEBUG("UPDNLMT: 0x%08x", fh_read32(g_reg_updnlmt[p2]));

			fh_switch2fhctl(p2, 1);

			fh_set_field(reg_cfg, FH_FRDDSX_EN, 1);	/* enable SSC mode */
			fh_set_field(reg_cfg, FH_FHCTLX_EN, 1);	/* enable hopping control */

			FH_MSG_DEBUG("CFG: 0x%08x", fh_read32(reg_cfg));
		}
		break;
	case 2222:
		/* TODO: and what this case for? */
		if (p2 == 0)	/* disable */
			mt_fh_hal_popod_save();
		else if (p2 == 1)	/* enable */
			mt_fh_hal_popod_restore();
		break;
	default:
		mt_fh_hal_dvfs(p1, p2);
		break;
	};

	return count;
}

/* #define UINT_MAX (unsigned int)(-1) */
static int fh_dumpregs_proc_read(struct seq_file *m, void *v)
{
	int i = 0;
	static unsigned int dds_max[FH_PLL_NUM] = { 0 };
	static unsigned int dds_min[FH_PLL_NUM] = {
		UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX,
		UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX
	};

	FH_MSG_DEBUG("EN: %s", __func__);

	for (i = 0; i < FH_PLL_NUM; ++i) {
		const unsigned int mon = DRV_Reg32(g_reg_mon[i]);
		const unsigned int dds = mon & MASK21b;

		seq_printf(m, "FHCTL%d CFG, UPDNLMT, DDS, MON\r\n", i);
		seq_printf(m, "0x%08x 0x%08x 0x%08x 0x%08x\r\n",
			   DRV_Reg32(g_reg_cfg[i]), DRV_Reg32(g_reg_updnlmt[i]),
			   DRV_Reg32(g_reg_dds[i]), mon);

		if (dds > dds_max[i])
			dds_max[i] = dds;
		if (dds < dds_min[i])
			dds_min[i] = dds;
	}

	seq_printf(m, "\r\nFHCTL_HP_EN:\r\n0x%08x\r\n", DRV_Reg32(REG_FHCTL_HP_EN));

	seq_puts(m, "\r\nPLL_CON0 :\r\n");
	seq_printf(m, "ARMCA7:0x%08x M:0x%08x MAIN:0x%08x MEM:0x%08x ",
		   DRV_Reg32(REG_ARMCA7PLL_CON0), DRV_Reg32(REG_MPLL_CON0),
		   DRV_Reg32(REG_MAINPLL_CON0), DRV_Reg32(REG_MEMPLL_CON0));
	seq_printf(m, "MSDC:0x%08x MM:0x%08x VENC:0x%08x TVD:0x%08x VCODEC:0x%08x\r\n",
		   DRV_Reg32(REG_MSDCPLL_CON0), DRV_Reg32(REG_MMPLL_CON0),
		   DRV_Reg32(REG_VENCPLL_CON0), DRV_Reg32(REG_TVDPLL_CON0),
		   DRV_Reg32(REG_VCODECPLL_CON0));


	seq_puts(m, "\r\nPLL_CON1 :\r\n");
	seq_printf(m, "ARMCA7:0x%08x M:0x%08x MAIN:0x%08x MEM:0x%08x ",
		   DRV_Reg32(REG_ARMCA7PLL_CON1), DRV_Reg32(REG_MPLL_CON1),
		   DRV_Reg32(REG_MAINPLL_CON1), DRV_Reg32(REG_MEMPLL_CON1));
	seq_printf(m, "MSDC:0x%08x MM:0x%08x VENC:0x%08x TVD:0x%08x VCODEC:0x%08x\r\n",
		   DRV_Reg32(REG_MSDCPLL_CON1), DRV_Reg32(REG_MMPLL_CON1),
		   DRV_Reg32(REG_VENCPLL_CON1), DRV_Reg32(REG_TVDPLL_CON1),
		   DRV_Reg32(REG_VCODECPLL_CON1));

	seq_puts(m, "\r\nRecorded dds range\r\n");

	for (i = 0; i < FH_PLL_NUM; ++i)
		seq_printf(m, "Pll%d dds max 0x%06x, min 0x%06x\r\n", i, dds_max[i], dds_min[i]);


	return 0;
}

static void __reg_tbl_init(void)
{
#ifdef CONFIG_OF
	int i = 0;

	const unsigned long reg_dds[] = {
		REG_FHCTL0_DDS, REG_FHCTL1_DDS, REG_FHCTL2_DDS,
		REG_FHCTL3_DDS, REG_FHCTL4_DDS, REG_FHCTL5_DDS,
		REG_FHCTL6_DDS, REG_FHCTL7_DDS, REG_FHCTL8_DDS
	};

	const unsigned long reg_cfg[] = {
		REG_FHCTL0_CFG, REG_FHCTL1_CFG, REG_FHCTL2_CFG,
		REG_FHCTL3_CFG, REG_FHCTL4_CFG, REG_FHCTL5_CFG,
		REG_FHCTL6_CFG, REG_FHCTL7_CFG, REG_FHCTL8_CFG
	};

	const unsigned long reg_updnlmt[] = {
		REG_FHCTL0_UPDNLMT, REG_FHCTL1_UPDNLMT, REG_FHCTL2_UPDNLMT,
		REG_FHCTL3_UPDNLMT, REG_FHCTL4_UPDNLMT, REG_FHCTL5_UPDNLMT,
		REG_FHCTL6_UPDNLMT, REG_FHCTL7_UPDNLMT, REG_FHCTL8_UPDNLMT
	};

	const unsigned long reg_mon[] = {
		REG_FHCTL0_MON, REG_FHCTL1_MON, REG_FHCTL2_MON,
		REG_FHCTL3_MON, REG_FHCTL4_MON, REG_FHCTL5_MON,
		REG_FHCTL6_MON, REG_FHCTL7_MON, REG_FHCTL8_MON
	};

	const unsigned long reg_dvfs[] = {
		REG_FHCTL0_DVFS, REG_FHCTL1_DVFS, REG_FHCTL2_DVFS,
		REG_FHCTL3_DVFS, REG_FHCTL4_DVFS, REG_FHCTL5_DVFS,
		REG_FHCTL6_DVFS, REG_FHCTL7_DVFS, REG_FHCTL8_DVFS
	};

	const unsigned long reg_pll_con1[] = {
		REG_ARMCA7PLL_CON1, REG_MPLL_CON1, REG_MAINPLL_CON1,
		REG_MEMPLL_CON1, REG_MSDCPLL_CON1, REG_MMPLL_CON1,
		REG_VENCPLL_CON1, REG_TVDPLL_CON1, REG_VCODECPLL_CON1
	};

	FH_MSG_DEBUG("EN: %s", __func__);


	for (i = 0; i < FH_PLL_NUM; ++i) {
		g_reg_dds[i] = reg_dds[i];
		g_reg_cfg[i] = reg_cfg[i];
		g_reg_updnlmt[i] = reg_updnlmt[i];
		g_reg_mon[i] = reg_mon[i];
		g_reg_dvfs[i] = reg_dvfs[i];
		g_reg_pll_con1[i] = reg_pll_con1[i];

	}
#endif
}

#ifdef CONFIG_OF
/* Device Tree Initialize */
static int __reg_base_addr_init(void)
{
	struct device_node *fhctl_node;
	struct device_node *apmixed_node;
	struct device_node *ddrphy_node;

	/* Init FHCTL base address */
	fhctl_node = of_find_compatible_node(NULL, NULL, "mediatek,FHCTL");
	if (!fhctl_node) {
		FH_MSG_DEBUG(" Error, Cannot find FHCTL device tree node");
		g_fhctl_base = (void *)FHCTL_BASE;
	} else {
		g_fhctl_base = of_iomap(fhctl_node, 0);
		if (!g_fhctl_base) {
			FH_MSG_DEBUG("Error, FHCTL iomap failed");
			g_fhctl_base = (void *)FHCTL_BASE;
		} else {
			/* FH_MSG_DEBUG("FHCTL dase address:0x%lx", (unsigned long)g_fhctl_base); */
		}
	}			/* if-else */

	/* Init APMIXED base address */
	apmixed_node = of_find_compatible_node(NULL, NULL, "mediatek,APMIXED");
	if (!fhctl_node) {
		FH_MSG_DEBUG(" Error, Cannot find APMIXED device tree node");
		g_apmixed_base = (void *)APMIXED_BASE;
	} else {
		g_apmixed_base = of_iomap(apmixed_node, 0);
		if (!g_apmixed_base) {
			FH_MSG_DEBUG("Error, APMIXED iomap failed");
			g_apmixed_base = (void *)APMIXED_BASE;
		} else {
			/* FH_MSG_DEBUG("APMIXED dase address:0x%lx", (unsigned long)g_apmixed_base); */
		}
	}			/* if-else */

	/* Init DDRPHY base address */
	ddrphy_node = of_find_compatible_node(NULL, NULL, "mediatek,DDRPHY");
	if (!fhctl_node) {
		FH_MSG_DEBUG(" Error, Cannot find FHCTL device tree node");
		g_ddrphy_base = (void *)DDRPHY_BASE;
	} else {
		g_ddrphy_base = of_iomap(ddrphy_node, 0);
		if (!g_ddrphy_base) {
			FH_MSG_DEBUG("Error, FHCTL DDRPHY failed");
			g_ddrphy_base = (void *)DDRPHY_BASE;
		} else {
			/* FH_MSG_DEBUG("DDRPHY dase address:0x%lx", (unsigned long)g_ddrphy_base); */
		}
	}			/* if-else */

	__reg_tbl_init();

	return 0;
}
#endif

/* TODO: __init void mt_freqhopping_init(void) */
static void mt_fh_hal_init(void)
{
	int i = 0;
	unsigned long flags = 0;


	FH_MSG_DEBUG("EN: %s", __func__);

	if (g_initialize == 1)
		return;

#ifdef CONFIG_OF

	/* Init relevant register base address by device tree */
	__reg_base_addr_init();
#endif


	for (i = 0; i < FH_PLL_NUM; ++i) {
		unsigned int mask = 1 << i;

		spin_lock_irqsave(&g_fh_lock, flags);

		/* TODO: clock should be turned on only when FH is needed */
		/* Turn on all clock */
		fh_set_field(REG_FHCTL_CLK_CON, mask, 1);

		/* Release software-reset to reset */
		fh_set_field(REG_FHCTL_RST_CON, mask, 0);
		fh_set_field(REG_FHCTL_RST_CON, mask, 1);

		g_fh_pll[i].setting_id = 0;
		fh_write32(g_reg_cfg[i], 0x00000000);	/* No SSC and FH enabled */
		fh_write32(g_reg_updnlmt[i], 0x00000000);	/* clear all the settings */
		fh_write32(g_reg_dds[i], 0x00000000);	/* clear all the settings */

		spin_unlock_irqrestore(&g_fh_lock, flags);
	}

	g_initialize = 1;
}

static void mt_fh_hal_lock(unsigned long *flags)
{
	spin_lock_irqsave(&g_fh_lock, *flags);
}

static void mt_fh_hal_unlock(unsigned long *flags)
{
	spin_unlock_irqrestore(&g_fh_lock, *flags);
}

static int mt_fh_hal_get_init(void)
{
	return g_initialize;
}

static int mt_fh_hal_is_support_DFS_mode(void)
{
	return TRUE;
}

/* TODO: module_init(mt_freqhopping_init); */
/* TODO: module_exit(cpufreq_exit); */

static int __fh_debug_proc_read(struct seq_file *m, void *v, fh_pll_t *pll)
{
	FH_MSG_DEBUG("EN: %s", __func__);

	seq_puts(m, "\r\n[freqhopping debug flag]\r\n");
	seq_puts(m, "===============================================\r\n");
	seq_printf(m,
		   "id=ARMCA7PLL=MPLL=MAINPLL=MEMPLL=MSDCPLL=MMPLL=VENCPLL=TVDPLL=VCODECPLL\r\n");
	seq_printf(m, "  =%04d==%04d==%04d==%04d==%04d==%04d==%04d==%04d==%04d=\r\n",
		   pll[FH_ARMCA7_PLLID].fh_status, pll[FH_M_PLLID].fh_status,
		   pll[FH_MAIN_PLLID].fh_status, pll[FH_MEM_PLLID].fh_status,
		   pll[FH_MSDC_PLLID].fh_status, pll[FH_MM_PLLID].fh_status,
		   pll[FH_VENC_PLLID].fh_status, pll[FH_TVD_PLLID].fh_status,
		   pll[FH_VCODEC_PLLID].fh_status);
	seq_printf(m, "  =%04d==%04d==%04d==%04d==%04d==%04d==%04d==%04d==%04d=\r\n",
		   pll[FH_ARMCA7_PLLID].setting_id, pll[FH_M_PLLID].setting_id,
		   pll[FH_MAIN_PLLID].setting_id, pll[FH_MEM_PLLID].setting_id,
		   pll[FH_MSDC_PLLID].setting_id, pll[FH_MM_PLLID].setting_id,
		   pll[FH_VENC_PLLID].setting_id, pll[FH_TVD_PLLID].setting_id,
		   pll[FH_VCODEC_PLLID].setting_id);

	return 0;
}


/* *********************************************************************** */
/* This function would support special request. */
/* [History] */
/* (2014.8.13)  K2 HQA desence SA required MEMPLL to enable SSC -2~-4%. */
/* We implement API mt_freqhopping_devctl() to */
/* complete -2~-4% SSC. (DVFS to -2% freq and enable 0~-2% SSC) */
/*  */
/* *********************************************************************** */
static int fh_ioctl_dvfs_ssc(unsigned int ctlid, void *arg)
{
	struct freqhopping_ioctl *fh_ctl = arg;

	switch (ctlid) {
	case FH_DCTL_CMD_DVFS:	/* < PLL DVFS */
		{
			mt_fh_hal_dvfs(fh_ctl->pll_id, fh_ctl->ssc_setting.dds);
		}
		break;
	case FH_DCTL_CMD_DVFS_SSC_ENABLE:	/* < PLL DVFS and enable SSC */
		{
			__disable_ssc(fh_ctl->pll_id, &(fh_ctl->ssc_setting));
			mt_fh_hal_dvfs(fh_ctl->pll_id, fh_ctl->ssc_setting.dds);
			__enable_ssc(fh_ctl->pll_id, &(fh_ctl->ssc_setting));
		}
		break;
	case FH_DCTL_CMD_DVFS_SSC_DISABLE:	/* < PLL DVFS and disable SSC */
		{
			__disable_ssc(fh_ctl->pll_id, &(fh_ctl->ssc_setting));
			mt_fh_hal_dvfs(fh_ctl->pll_id, fh_ctl->ssc_setting.dds);
		}
		break;
	case FH_DCTL_CMD_SSC_ENABLE:	/* < SSC enable */
		{
			__enable_ssc(fh_ctl->pll_id, &(fh_ctl->ssc_setting));
		}
		break;
	case FH_DCTL_CMD_SSC_DISABLE:	/* < SSC disable */
		{
			__disable_ssc(fh_ctl->pll_id, &(fh_ctl->ssc_setting));
		}
		break;
	default:
		break;
	};

	return 0;
}


static void __ioctl(unsigned int ctlid, void *arg)
{
	switch (ctlid) {
	case FH_IO_PROC_READ:
		{
			FH_IO_PROC_READ_T *tmp = (FH_IO_PROC_READ_T *) (arg);
			__fh_debug_proc_read(tmp->m, tmp->v, tmp->pll);
		}
		break;
	case FH_DCTL_CMD_DVFS:	/* < PLL DVFS */
	case FH_DCTL_CMD_DVFS_SSC_ENABLE:	/* < PLL DVFS and enable SSC */
	case FH_DCTL_CMD_DVFS_SSC_DISABLE:	/* < PLL DVFS and disable SSC */
	case FH_DCTL_CMD_SSC_ENABLE:	/* < SSC enable */
	case FH_DCTL_CMD_SSC_DISABLE:	/* < SSC disable */
		{
			fh_ioctl_dvfs_ssc(ctlid, arg);
		}
		break;

	default:
		FH_MSG_DEBUG("Unrecognized ctlid %d", ctlid);
		break;
	};
}

static struct mt_fh_hal_driver g_fh_hal_drv = {
	.fh_pll = g_fh_pll,
	.fh_usrdef = mt_ssc_fhpll_userdefined,
	.mempll = FH_M_PLLID,
	.lvdspll = FH_MAX_PLLID + 1,
	.mainpll = FH_MAIN_PLLID,
	.msdcpll = FH_MSDC_PLLID,
	.mmpll = FH_MM_PLLID,
	.vencpll = FH_VENC_PLLID,
	.pll_cnt = FH_PLL_NUM,
	.proc.clk_gen_read = NULL,
	.proc.clk_gen_write = NULL,
	.proc.dramc_read = fh_dramc_proc_read,
	.proc.dramc_write = fh_dramc_proc_write,
	.proc.dumpregs_read = fh_dumpregs_proc_read,
	.proc.dvfs_read = fh_dvfs_proc_read,
	.proc.dvfs_write = fh_dvfs_proc_write,
	.mt_fh_hal_init = mt_fh_hal_init,
	.mt_fh_hal_ctrl = __freqhopping_ctrl,
	.mt_fh_lock = mt_fh_hal_lock,
	.mt_fh_unlock = mt_fh_hal_unlock,
	.mt_fh_get_init = mt_fh_hal_get_init,
	.mt_fh_popod_restore = mt_fh_hal_popod_restore,
	.mt_fh_popod_save = mt_fh_hal_popod_save,
	.mt_l2h_mempll = NULL,
	.mt_h2l_mempll = NULL,
	.mt_dfs_armpll = mt_fh_hal_dfs_armpll,
	.mt_dfs_mmpll = mt_fh_hal_dfs_mmpll,
	.mt_dfs_vencpll = mt_fh_hal_dfs_vencpll,	/* TODO: should set to NULL */
	.mt_is_support_DFS_mode = mt_fh_hal_is_support_DFS_mode,
	.mt_l2h_dvfs_mempll = mt_fh_hal_l2h_dvfs_mempll,	/* TODO: should set to NULL */
	.mt_h2l_dvfs_mempll = mt_fh_hal_h2l_dvfs_mempll,	/* TODO: should set to NULL */
	.mt_dram_overclock = mt_fh_hal_dram_overclock,
	.mt_get_dramc = mt_fh_hal_get_dramc,
	.mt_fh_default_conf = mt_fh_hal_default_conf,
	.ioctl = __ioctl
};

struct mt_fh_hal_driver *mt_get_fh_hal_drv(void)
{
	return (&g_fh_hal_drv);
}

/* TODO: module_exit(cpufreq_exit); */
