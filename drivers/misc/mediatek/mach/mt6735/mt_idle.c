#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpu.h>

#include <linux/types.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mtk_gpu_utility.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <mach/mt_cirq.h>
#include <asm/system_misc.h>
#include <mach/mt_typedefs.h>
#include <mach/sync_write.h>
#include <mach/mt_dcm.h>
#include <mach/mt_gpt.h>
#include <mach/mt_cpuxgpt.h>
#include <mach/mt_spm.h>
#include <mach/mt_spm_idle.h>
#include <mach/mt_spm_sleep.h>
#include <mach/hotplug.h>
#include <mach/mt_cpufreq.h>
#include <mach/mt_power_gs.h>
#include <mach/mt_ptp.h>
#include <mach/mt_timer.h>
#include <mach/irqs.h>
#include <mach/mt_thermal.h>
#include <mach/mt_idle.h>
#include <mach/mt_boot.h>
#include <mach/mt_mmc.h>

#define IDLE_TAG     "[Power/swap]"
#define spm_emerg(fmt, args...)		pr_emerg(IDLE_TAG fmt, ##args)
#define spm_alert(fmt, args...)		pr_alert(IDLE_TAG fmt, ##args)
#define spm_crit(fmt, args...)		pr_crit(IDLE_TAG fmt, ##args)
#define idle_err(fmt, args...)		pr_err(IDLE_TAG fmt, ##args)
#define idle_warn(fmt, args...)		pr_warn(IDLE_TAG fmt, ##args)
#define spm_notice(fmt, args...)	pr_notice(IDLE_TAG fmt, ##args)
#define idle_info(fmt, args...)		pr_info(IDLE_TAG fmt, ##args)
#define idle_ver(fmt, args...)		pr_debug(IDLE_TAG fmt, ##args)
#define idle_dbg(fmt, args...)		pr_debug(IDLE_TAG fmt, ##args)

#define idle_gpt GPT4

#define idle_readl(addr) \
    DRV_Reg32(addr)

#define idle_writel(addr, val)   \
    mt65xx_reg_sync_writel(val, addr)

#define idle_setl(addr, val) \
    mt65xx_reg_sync_writel(idle_readl(addr) | (val), addr)

#define idle_clrl(addr, val) \
    mt65xx_reg_sync_writel(idle_readl(addr) & ~(val), addr)


extern unsigned long localtimer_get_counter(void);
extern int localtimer_set_next_event(unsigned long evt);
extern void hp_enable_timer(int enable);

enum {
	CG_INFRA   = 0,
	CG_PERI    = 1,
	CG_DISP0   = 2,
	CG_DISP1   = 3,
	CG_IMAGE   = 4,
	CG_MFG     = 5,
	CG_AUDIO   = 6,
	CG_VDEC0   = 7,
	CG_VDEC1   = 8,
	CG_VENC    = 9,
	NR_GRPS    = 10,
};

static unsigned long rgidle_cnt[NR_CPUS] = {0};
static bool mt_idle_chk_golden = 0;
static bool mt_dpidle_chk_golden = 0;


#define INVALID_GRP_ID(grp) (grp < 0 || grp >= NR_GRPS)


/* FIXME: early porting */
#if 1
void __attribute__((weak))
bus_dcm_enable(void)
{
	/* FIXME: early porting */
}
void __attribute__((weak))
bus_dcm_disable(void)
{
	/* FIXME: early porting */
}
void __attribute__((weak))
tscpu_cancel_thermal_timer(void)
{
	/* FIXME: early porting */
}

void __attribute__((weak))
tscpu_start_thermal_timer(void)
{
	/* FIXME: early porting */
}
#endif


enum {
    IDLE_TYPE_DP = 0,
    IDLE_TYPE_SO = 1,
    IDLE_TYPE_SL = 2,
    IDLE_TYPE_RG = 3,
    NR_TYPES	= 4,
};

enum {
    BY_CPU = 0,
    BY_CLK = 1,
    BY_TMR = 2,
    BY_OTH = 3,
    BY_VTG = 4,
    NR_REASONS = 5
};

/*Idle handler on/off*/
static int idle_switch[NR_TYPES] = {
	1,  /* dpidle switch */
	1,  /* soidle switch */
	1,  /* slidle switch */
	1,  /* rgidle switch */
};

static unsigned int dpidle_condition_mask[NR_GRPS] = {
	0x0000008A, /* INFRA: */
	0x37FC1FFD, /* PERI0: */
	0x000FFFFF, /* DISP0: */
	0x0000003C, /* DISP1: */
	0x00000FE1, /* IMAGE: */
	0x00000001, /* MFG:   */
	0x00000000, /* AUDIO: */
	0x00000001, /* VDEC0: */
	0x00000001, /* VDEC1: */
	0x00001111, /* VENC:  */
};

static unsigned int soidle_condition_mask[NR_GRPS] = {
    0x00000088, //INFRA:
    0x37FC0FFC, //PERI0:
    0x000033FC, //DISP0:
    0x00000030, //DISP1:
    0x00000FE1, //IMAGE:
    0x00000001, //MFG:
    0x00000000, //AUDIO:
    0x00000001, //VDEC0:
    0x00000001, //VDEC1:
    0x00001111, //VENC:
};

static unsigned int slidle_condition_mask[NR_GRPS] = {
    0x00000000, //INFRA:
    0x07C01000, //PERI0:
    0x00000000, //DISP0:
    0x00000000, //DISP1:
    0x00000000, //IMAGE:
    0x00000000, //MFG:
    0x00000000, //AUDIO:
    0x00000000, //VDEC0:
    0x00000000, //VDEC1:
    0x00000000, //VENC:
};

static const char *idle_name[NR_TYPES] = {
    "dpidle",
    "soidle",
    "slidle",
    "rgidle",
};

static const char *reason_name[NR_REASONS] = {
    "by_cpu",
    "by_clk",
    "by_tmr",
    "by_oth",
    "by_vtg",
};

char cg_group_name[][NR_GRPS] = {
	"INFRA",
	"PERI",
	"DISP0",
	"DISP1",
	"IMAGE",
	"MFG",
	"AUDIO",
	"VDEC0",
	"VDEC1",
	"VENC",
};

/*Slow Idle*/
static unsigned int slidle_block_mask[NR_GRPS] = {0x0};
static unsigned long slidle_cnt[NR_CPUS] = {0};
static unsigned long slidle_block_cnt[NR_REASONS] = {0};

/*SODI*/
static unsigned int     soidle_block_mask[NR_GRPS] = {0x0};
static unsigned int     soidle_timer_left;
static unsigned int     soidle_timer_left2;
#ifndef CONFIG_SMP
static unsigned int     soidle_timer_cmp;
#endif
static unsigned int     soidle_time_critera = 26000; //FIXME: need fine tune
static unsigned int     soidle_block_time_critera = 30000;//default 30sec
static unsigned long    soidle_cnt[NR_CPUS] = {0};
static unsigned long    soidle_block_cnt[NR_CPUS][NR_REASONS] = {{0}};
static unsigned long long soidle_block_prev_time = 0;
static bool             soidle_by_pass_cg = 0;

/*DeepIdle*/
static unsigned int     dpidle_block_mask[NR_GRPS] = {0x0};
static unsigned int     dpidle_timer_left;
static unsigned int     dpidle_timer_left2;
#ifndef CONFIG_SMP
static unsigned int     dpidle_timer_cmp;
#endif
static unsigned int     dpidle_time_critera = 26000;
static unsigned int     dpidle_block_time_critera = 30000;//default 30sec
static unsigned long    dpidle_cnt[NR_CPUS] = {0};
static unsigned long    dpidle_block_cnt[NR_REASONS] = {0};
static unsigned long long dpidle_block_prev_time = 0;
static bool             dpidle_by_pass_cg = 0;

static unsigned int 	idle_spm_lock = 0;

const char *cg_grp_get_name(int id)
{
	BUG_ON(INVALID_GRP_ID(id));
	return cg_group_name[id];
}

static long int idle_get_current_time_ms(void)
{
    struct timeval t;
    do_gettimeofday(&t);
    return ((t.tv_sec & 0xFFF) * 1000000 + t.tv_usec) / 1000;
}

static DEFINE_SPINLOCK(idle_spm_spin_lock);

void idle_lock_spm(enum idle_lock_spm_id id)
{
    unsigned long flags;
    spin_lock_irqsave(&idle_spm_spin_lock, flags);
    idle_spm_lock|=(1<<id);
    spin_unlock_irqrestore(&idle_spm_spin_lock, flags);
}

void idle_unlock_spm(enum idle_lock_spm_id id)
{
    unsigned long flags;
    spin_lock_irqsave(&idle_spm_spin_lock, flags);
    idle_spm_lock&=~(1<<id);
    spin_unlock_irqrestore(&idle_spm_spin_lock, flags);
}

/************************************************
 * SODI part
 ************************************************/
static DEFINE_MUTEX(soidle_locked);

static void enable_soidle_by_mask(int grp, unsigned int mask)
{
    mutex_lock(&soidle_locked);
    soidle_condition_mask[grp] &= ~mask;
    mutex_unlock(&soidle_locked);
}

static void disable_soidle_by_mask(int grp, unsigned int mask)
{
    mutex_lock(&soidle_locked);
    soidle_condition_mask[grp] |= mask;
    mutex_unlock(&soidle_locked);
}

void enable_soidle_by_bit(int id)
{
    int grp = id / 32;
    unsigned int mask = 1U << (id % 32);
    BUG_ON(INVALID_GRP_ID(grp));
    enable_soidle_by_mask(grp, mask);
}
EXPORT_SYMBOL(enable_soidle_by_bit);

void disable_soidle_by_bit(int id)
{
    int grp = id / 32;
    unsigned int mask = 1U << (id % 32);
    BUG_ON(INVALID_GRP_ID(grp));
    disable_soidle_by_mask(grp, mask);
}
EXPORT_SYMBOL(disable_soidle_by_bit);

#define SODI_DVT                    0

#if SODI_DVT
#define SODI_APxGPT_TimerCount      0
#define SODI_DISPLAY_DRV_CHK_DIS    0
#define SODI_CG_CHK_DIS             1
#endif

static bool soidle_can_enter(int cpu)
{
    int reason = NR_REASONS;
    unsigned long long soidle_block_curr_time = 0;

#ifdef CONFIG_SMP
    if ((atomic_read(&is_in_hotplug) == 1)||(num_online_cpus() != 1)) {
        reason = BY_CPU;
        goto out;
    }
#endif

    if(idle_spm_lock){
        reason = BY_VTG;
        goto out;
    }

#if !defined(SODI_DISPLAY_DRV_CHK_DIS) || (SODI_DISPLAY_DRV_CHK_DIS == 0)
    // decide when to enable SODI by display driver
    if(spm_get_sodi_en()==0){
        reason = BY_OTH;
        goto out;
	}
#endif

	if (soidle_by_pass_cg == 0) {
		memset(soidle_block_mask, 0, NR_GRPS * sizeof(unsigned int));
		if (!cg_check_idle_can_enter(soidle_condition_mask, soidle_block_mask)) {
#if !defined(SODI_CG_CHK_DIS) || (SODI_CG_CHK_DIS == 0)
			reason = BY_CLK;
			goto out;
#endif
		}
	}

#ifdef CONFIG_SMP
    soidle_timer_left = localtimer_get_counter();
    if ((int)soidle_timer_left < soidle_time_critera ||
            ((int)soidle_timer_left) < 0) {
        reason = BY_TMR;
        goto out;
    }
#else
    gpt_get_cnt(GPT1, &soidle_timer_left);
    gpt_get_cmp(GPT1, &soidle_timer_cmp);
    if((soidle_timer_cmp-soidle_timer_left)<soidle_time_critera)
    {
        reason = BY_TMR;
        goto out;
    }
#endif

out:
    if (reason < NR_REASONS) {
        if( soidle_block_prev_time == 0 )
            soidle_block_prev_time = idle_get_current_time_ms();

        soidle_block_curr_time = idle_get_current_time_ms();
        if((soidle_block_curr_time - soidle_block_prev_time) > soidle_block_time_critera)
        {
            if ((smp_processor_id() == 0))
            {
                int i = 0;

                for (i = 0; i < nr_cpu_ids; i++) {
                    idle_ver("soidle_cnt[%d]=%lu, rgidle_cnt[%d]=%lu\n",
                            i, soidle_cnt[i], i, rgidle_cnt[i]);
                }

                for (i = 0; i < NR_REASONS; i++) {
                    idle_ver("[%d]soidle_block_cnt[0][%s]=%lu\n", i, reason_name[i],
                            soidle_block_cnt[0][i]);
                }

                for (i = 0; i < NR_GRPS; i++) {
                    idle_ver("[%02d]soidle_condition_mask[%-8s]=0x%08x\t\t"
                            "soidle_block_mask[%-8s]=0x%08x\n", i,
							cg_grp_get_name(i), soidle_condition_mask[i],
							cg_grp_get_name(i), soidle_block_mask[i]);
                }

                memset(soidle_block_cnt, 0, sizeof(soidle_block_cnt));
                soidle_block_prev_time = idle_get_current_time_ms();
            }
        }

        soidle_block_cnt[cpu][reason]++;
        return false;
    } else {
        soidle_block_prev_time = idle_get_current_time_ms();
        return true;
    }
}

void soidle_before_wfi(int cpu)
{
#ifdef CONFIG_SMP
	#if !defined(SODI_APxGPT_TimerCount) || (SODI_APxGPT_TimerCount == 0)
	soidle_timer_left2 = localtimer_get_counter();
	#else
	soidle_timer_left2 = 13000000*SODI_APxGPT_TimerCount;
	#endif

    if( (int)soidle_timer_left2 <=0 )
    {
        gpt_set_cmp(idle_gpt, 1);//Trigger idle_gpt Timerout imediately
    }
    else
    {
        gpt_set_cmp(idle_gpt, soidle_timer_left2);
    }

    start_gpt(idle_gpt);
#else
    gpt_get_cnt(GPT1, &soidle_timer_left2);
#endif

}

void soidle_after_wfi(int cpu)
{
#ifdef CONFIG_SMP
    if (gpt_check_and_ack_irq(idle_gpt)) {
        localtimer_set_next_event(1);
    } else {
        /* waked up by other wakeup source */
        unsigned int cnt, cmp;
        gpt_get_cnt(idle_gpt, &cnt);
        gpt_get_cmp(idle_gpt, &cmp);
        if (unlikely(cmp < cnt)) {
            idle_err("[%s]GPT%d: counter = %10u, compare = %10u\n", __func__,
                    idle_gpt + 1, cnt, cmp);
            BUG();
        }

        localtimer_set_next_event(cmp-cnt);
        stop_gpt(idle_gpt);
    }
#endif
    soidle_cnt[cpu]++;
}

/************************************************
 * deep idle part
 ************************************************/
static DEFINE_MUTEX(dpidle_locked);

static void enable_dpidle_by_mask(int grp, unsigned int mask)
{
    mutex_lock(&dpidle_locked);
    dpidle_condition_mask[grp] &= ~mask;
    mutex_unlock(&dpidle_locked);
}

static void disable_dpidle_by_mask(int grp, unsigned int mask)
{
    mutex_lock(&dpidle_locked);
    dpidle_condition_mask[grp] |= mask;
    mutex_unlock(&dpidle_locked);
}

void enable_dpidle_by_bit(int id)
{
    int grp = id / 32;
    unsigned int mask = 1U << (id % 32);
    BUG_ON(INVALID_GRP_ID(grp));
    enable_dpidle_by_mask(grp, mask);
}
EXPORT_SYMBOL(enable_dpidle_by_bit);

void disable_dpidle_by_bit(int id)
{
    int grp = id / 32;
    unsigned int mask = 1U << (id % 32);
    BUG_ON(INVALID_GRP_ID(grp));
    disable_dpidle_by_mask(grp, mask);
}
EXPORT_SYMBOL(disable_dpidle_by_bit);

static bool dpidle_can_enter(void)
{
    int reason = NR_REASONS;
    int i = 0;
    unsigned long long dpidle_block_curr_time = 0;

#if 0
    if(dpidle_by_pass_cg==0){
        if (!mt_cpufreq_earlysuspend_status_get()){
            reason = BY_VTG;
            goto out;
        }
    }
#endif

#ifdef CONFIG_SMP
    if ((atomic_read(&is_in_hotplug) >= 1)||(num_online_cpus() != 1)) {
        reason = BY_CPU;
        goto out;
    }
#endif

    if(idle_spm_lock){
        reason = BY_VTG;
        goto out;
    }

    if(dpidle_by_pass_cg==0){
	    memset(dpidle_block_mask, 0, NR_GRPS * sizeof(unsigned int));
	    if (!cg_check_idle_can_enter(dpidle_condition_mask, dpidle_block_mask)) {
	        reason = BY_CLK;
	        goto out;
	    }
    }
#ifdef CONFIG_SMP
    dpidle_timer_left = localtimer_get_counter();
    if ((int)dpidle_timer_left < dpidle_time_critera ||
            ((int)dpidle_timer_left) < 0) {
        reason = BY_TMR;
        goto out;
    }
#else
    gpt_get_cnt(GPT1, &dpidle_timer_left);
    gpt_get_cmp(GPT1, &dpidle_timer_cmp);
    if((dpidle_timer_cmp-dpidle_timer_left)<dpidle_time_critera)
    {
        reason = BY_TMR;
        goto out;
    }
#endif

out:
    if (reason < NR_REASONS) {
        if( dpidle_block_prev_time == 0 )
            dpidle_block_prev_time = idle_get_current_time_ms();

        dpidle_block_curr_time = idle_get_current_time_ms();
        if((dpidle_block_curr_time - dpidle_block_prev_time) > dpidle_block_time_critera)
        {
            if ((smp_processor_id() == 0))
            {
                for (i = 0; i < nr_cpu_ids; i++) {
                    idle_ver("dpidle_cnt[%d]=%lu, rgidle_cnt[%d]=%lu\n",
                            i, dpidle_cnt[i], i, rgidle_cnt[i]);
                }

                for (i = 0; i < NR_REASONS; i++) {
                    idle_ver("[%d]dpidle_block_cnt[%s]=%lu\n", i, reason_name[i],
                            dpidle_block_cnt[i]);
                }

                for (i = 0; i < NR_GRPS; i++) {
                    idle_ver("[%02d]dpidle_condition_mask[%-8s]=0x%08x\t\t"
                            "dpidle_block_mask[%-8s]=0x%08x\n", i,
							cg_grp_get_name(i), dpidle_condition_mask[i],
							cg_grp_get_name(i), dpidle_block_mask[i]);
                }
                memset(dpidle_block_cnt, 0, sizeof(dpidle_block_cnt));
                dpidle_block_prev_time = idle_get_current_time_ms();

            }


        }
        dpidle_block_cnt[reason]++;
        return false;
    } else {
        dpidle_block_prev_time = idle_get_current_time_ms();
        return true;
    }

}

void spm_dpidle_before_wfi(void)
{
    if (TRUE == mt_dpidle_chk_golden)
    {
    //FIXME:
#if 0
        mt_power_gs_dump_dpidle();
#endif
    }
    bus_dcm_enable();

	faudintbus_pll2sq();
    //clkmux_sel(MT_MUX_AUDINTBUS, 0, "Deepidle"); //select 26M


#ifdef CONFIG_SMP
    dpidle_timer_left2 = localtimer_get_counter();

    if( (int)dpidle_timer_left2 <=0 )
        gpt_set_cmp(idle_gpt, 1);//Trigger GPT4 Timerout imediately
    else
        gpt_set_cmp(idle_gpt, dpidle_timer_left2);

    start_gpt(idle_gpt);
#else
    gpt_get_cnt(idle_gpt, &dpidle_timer_left2);
#endif
}

void spm_dpidle_after_wfi(void)
{
#ifdef CONFIG_SMP
        //if (gpt_check_irq(GPT4)) {
        if (gpt_check_and_ack_irq(idle_gpt)) {
            /* waked up by WAKEUP_GPT */
            localtimer_set_next_event(1);
        } else {
            /* waked up by other wakeup source */
            unsigned int cnt, cmp;
            gpt_get_cnt(idle_gpt, &cnt);
            gpt_get_cmp(idle_gpt, &cmp);
            if (unlikely(cmp < cnt)) {
                idle_err("[%s]GPT%d: counter = %10u, compare = %10u\n", __func__,
                        idle_gpt + 1, cnt, cmp);
                BUG();
            }

            localtimer_set_next_event(cmp-cnt);
            stop_gpt(idle_gpt);
            //GPT_ClearCount(WAKEUP_GPT);
        }
#endif

    //clkmux_sel(MT_MUX_AUDINTBUS, 1, "Deepidle"); //mainpll

	faudintbus_sq2pll();

    bus_dcm_disable();

    dpidle_cnt[0]++;
}

/************************************************
 * slow idle part
 ************************************************/

static DEFINE_MUTEX(slidle_locked);

static void enable_slidle_by_mask(int grp, unsigned int mask)
{
    mutex_lock(&slidle_locked);
    slidle_condition_mask[grp] &= ~mask;
    mutex_unlock(&slidle_locked);
}

static void disable_slidle_by_mask(int grp, unsigned int mask)
{
    mutex_lock(&slidle_locked);
    slidle_condition_mask[grp] |= mask;
    mutex_unlock(&slidle_locked);
}

void enable_slidle_by_bit(int id)
{
    int grp = id / 32;
    unsigned int mask = 1U << (id % 32);
    BUG_ON(INVALID_GRP_ID(grp));
    enable_slidle_by_mask(grp, mask);
}
EXPORT_SYMBOL(enable_slidle_by_bit);

void disable_slidle_by_bit(int id)
{
    int grp = id / 32;
    unsigned int mask = 1U << (id % 32);
    BUG_ON(INVALID_GRP_ID(grp));
    disable_slidle_by_mask(grp, mask);
}
EXPORT_SYMBOL(disable_slidle_by_bit);

#if EN_PTP_OD
extern u32 ptp_data[3];
#endif

static bool slidle_can_enter(void)
{
    int reason = NR_REASONS;

    if ((atomic_read(&is_in_hotplug) == 1)||(num_online_cpus() != 1)) {
        reason = BY_CPU;
        goto out;
    }

    memset(slidle_block_mask, 0, NR_GRPS * sizeof(unsigned int));
	if (!cg_check_idle_can_enter(slidle_condition_mask, slidle_block_mask)) {
		reason = BY_CLK;
		goto out;
    }

#if EN_PTP_OD
    if (ptp_data[0]) {
        reason = BY_OTH;
        goto out;
    }
#endif

out:
    if (reason < NR_REASONS) {
        slidle_block_cnt[reason]++;
        return false;
    } else {
        return true;
    }
}

static void slidle_before_wfi(int cpu)
{
    mt_dcm_topckg_enable();
}

static void slidle_after_wfi(int cpu)
{
    mt_dcm_topckg_disable();
    slidle_cnt[cpu]++;
}

static void go_to_slidle(int cpu)
{
    slidle_before_wfi(cpu);

    dsb();
    __asm__ __volatile__("wfi" ::: "memory");

    slidle_after_wfi(cpu);
}

/************************************************
 * regular idle part
 ************************************************/
static void rgidle_before_wfi(int cpu)
{

}

static void rgidle_after_wfi(int cpu)
{
    rgidle_cnt[cpu]++;
}

static void noinline go_to_rgidle(int cpu)
{
    rgidle_before_wfi(cpu);
    isb();
    dsb();
    __asm__ __volatile__("wfi" ::: "memory");

    rgidle_after_wfi(cpu);
}

/************************************************
 * idle task flow part
 ************************************************/
extern bool mtk_gpu_sodi_entry(void); 
extern bool mtk_gpu_sodi_exit(void);

static inline void soidle_pre_handler(void)
{
    //stop Mali dvfs_callback timer
    if (!mtk_gpu_sodi_entry())
    {
        idle_ver("not stop GPU timer in SODI\n");
    }
    
#ifndef CONFIG_MTK_FPGA
#ifdef CONFIG_THERMAL
    //cancel thermal hrtimer for power saving
    tscpu_cancel_thermal_timer();

	mtkts_bts_cancel_thermal_timer();
    mtkts_btsmdpa_cancel_thermal_timer();
    mtkts_pmic_cancel_thermal_timer();
    mtkts_battery_cancel_thermal_timer();
    mtkts_pa_cancel_thermal_timer();
	mtkts_allts_cancel_thermal_timer();
    mtkts_wmt_cancel_thermal_timer();
#endif
#endif
}

static inline void soidle_post_handler(void)
{
    //restart Mali dvfs_callback timer
    if (!mtk_gpu_sodi_exit())
    {
        idle_ver("not restart GPU timer outside SODI\n");
    }
    
#ifndef CONFIG_MTK_FPGA
#ifdef CONFIG_THERMAL
    //restart thermal hrtimer for update temp info
    tscpu_start_thermal_timer();

	mtkts_bts_start_thermal_timer();
    mtkts_btsmdpa_start_thermal_timer();
    mtkts_pmic_start_thermal_timer();
    mtkts_battery_start_thermal_timer();
    mtkts_pa_start_thermal_timer();
	mtkts_allts_start_thermal_timer();
    mtkts_wmt_start_thermal_timer();
#endif
#endif
}
/*
 * xxidle_handler return 1 if enter and exit the low power state
 */

static u32 slp_spm_SODI_flags = {
	0//SPM_MD_VRF18_DIS
};

//#define DEFAULT_MMP_ENABLE
#ifdef DEFAULT_MMP_ENABLE
extern void MMProfileEnable(int enable);
extern void MMProfileStart(int start);

static SODI_MMP_Events_t SODI_MMP_Events;

void init_sodi_mmp_events(void)
{
	if (SODI_MMP_Events.SODI == 0)
	{
		SODI_MMP_Events.SODI = MMProfileRegisterEvent(MMP_RootEvent, "SODI");
		SODI_MMP_Events.sodi_enable = MMProfileRegisterEvent(SODI_MMP_Events.SODI, "sodi_enable");
		SODI_MMP_Events.self_refresh_cnt = MMProfileRegisterEvent(SODI_MMP_Events.SODI, "self_refresh_cnt");
		SODI_MMP_Events.sodi_status  = MMProfileRegisterEvent(SODI_MMP_Events.SODI, "sodi_status");				  
		
		MMProfileEnableEventRecursive(SODI_MMP_Events.SODI, 1);
		MMProfileEnableEventRecursive(SODI_MMP_Events.sodi_enable, 1);       
		MMProfileEnableEventRecursive(SODI_MMP_Events.self_refresh_cnt, 1);
		MMProfileEnableEventRecursive(SODI_MMP_Events.sodi_status, 1);
	}
}

SODI_MMP_Events_t * sodi_mmp_get_events(void)
{
    return &SODI_MMP_Events;
}

void sodi_mmp_init(void)
{
    printk("sodi_mmp_init\n");
    MMProfileEnable(1);
    init_sodi_mmp_events();
    MMProfileStart(1);
}
#endif //DEFAULT_MMP_ENABLE

u32 slp_spm_deepidle_flags = {
	0 /* SPM_MD_VRF18_DIS//SPM_CPU_PDN_DIS */
	/* SPM_MD_VRF18_DIS|SPM_CPU_PDN_DIS|SPM_CPU_DVS_DIS|SPM_DISABLE_ATF_ABORT */
};

static inline void dpidle_pre_handler(void)
{
#ifndef CONFIG_MTK_FPGA
#ifdef CONFIG_THERMAL
    //cancel thermal hrtimer for power saving
    tscpu_cancel_thermal_timer();
	mtkts_bts_cancel_thermal_timer();
    mtkts_btsmdpa_cancel_thermal_timer();
    mtkts_pmic_cancel_thermal_timer();
    mtkts_battery_cancel_thermal_timer();
    mtkts_pa_cancel_thermal_timer();
	mtkts_allts_cancel_thermal_timer();
    mtkts_wmt_cancel_thermal_timer();
#endif
#endif
#if 0
    // disable gpu dvfs timer
    mtk_enable_gpu_dvfs_timer(false);

    // disable cpu dvfs timer
    hp_enable_timer(0);
#endif

}
static inline void dpidle_post_handler(void)
{
#if 0
    // disable cpu dvfs timer
    hp_enable_timer(1);

    // enable gpu dvfs timer
    mtk_enable_gpu_dvfs_timer(true);
#endif
#ifndef CONFIG_MTK_FPGA
#ifdef CONFIG_THERMAL
    //restart thermal hrtimer for update temp info
    tscpu_start_thermal_timer();
	mtkts_bts_start_thermal_timer();
    mtkts_btsmdpa_start_thermal_timer();
    mtkts_pmic_start_thermal_timer();
    mtkts_battery_start_thermal_timer();
    mtkts_pa_start_thermal_timer();
	mtkts_allts_start_thermal_timer();
    mtkts_wmt_start_thermal_timer();
#endif
#endif
}
#ifdef SPM_DEEPIDLE_PROFILE_TIME
unsigned int dpidle_profile[4];
#endif

static inline int dpidle_select_handler(int cpu)
{
	int ret = 0;

	if (idle_switch[IDLE_TYPE_DP]) {
		if (dpidle_can_enter())
			ret = 1;
	}

	return ret;
}

static inline int soidle_select_handler(int cpu)
{
	int ret = 0;

	if (idle_switch[IDLE_TYPE_SO]) {
		if (soidle_can_enter(cpu))
			ret = 1;
	}

	return ret;
}

static inline int slidle_select_handler(int cpu)
{
	int ret = 0;

	if (idle_switch[IDLE_TYPE_SL]) {
		if (slidle_can_enter())
			ret = 1;
	}

	return ret;
}

static inline int rgidle_select_handler(int cpu)
{
	int ret = 0;

	if (idle_switch[IDLE_TYPE_RG])
		ret = 1;

	return ret;
}

static int (*idle_select_handlers[NR_TYPES])(int) = {
	dpidle_select_handler,
	soidle_select_handler,
	slidle_select_handler,
	rgidle_select_handler,
};

int mt_idle_select(int cpu)
{
	int i = NR_TYPES - 1;

	for (i = 0; i < NR_TYPES; i++) {
		if (idle_select_handlers[i](cpu))
			break;
	}

	return i;
}
EXPORT_SYMBOL(mt_idle_select);

int dpidle_enter(int cpu)
{
	int ret = 1;

	dpidle_pre_handler();
	spm_go_to_dpidle(slp_spm_deepidle_flags, 0);
	dpidle_post_handler();

#ifdef CONFIG_SMP
	idle_dbg("DP:timer_left=%d, timer_left2=%d, delta=%d\n",
				dpidle_timer_left, dpidle_timer_left2, dpidle_timer_left-dpidle_timer_left2);
#else
	idle_dbg("DP:timer_left=%d, timer_left2=%d, delta=%d, timeout val=%d\n",
				dpidle_timer_left,
				dipidle_timer_left2,
				dpidle_timer_left2 - dpidle_timer_left,
				dpidle_timer_cmp - dpidle_timer_left);
#endif
#ifdef SPM_DEEPIDLE_PROFILE_TIME
	gpt_get_cnt(SPM_PROFILE_APXGPT, &dpidle_profile[3]);
	idle_ver("1:%u, 2:%u, 3:%u, 4:%u\n",
				dpidle_profile[0], dpidle_profile[1], dpidle_profile[2], dpidle_profile[3]);
#endif

	return ret;
}
EXPORT_SYMBOL(dpidle_enter);

int soidle_enter(int cpu)
{
	int ret = 1;

	soidle_pre_handler();
	spm_go_to_sodi(slp_spm_SODI_flags, 0);
	soidle_post_handler();

	return ret;
}
EXPORT_SYMBOL(soidle_enter);

int slidle_enter(int cpu)
{
	int ret = 1;

	go_to_slidle(cpu);

	return ret;
}
EXPORT_SYMBOL(slidle_enter);

int rgidle_enter(int cpu)
{
	int ret = 1;

	go_to_rgidle(cpu);

	return ret;
}
EXPORT_SYMBOL(rgidle_enter);

void mt_idle_init(void)
{

}

/***************************/
/* debugfs                 */
/***************************/
static char dbg_buf[2048] = {0};
static char cmd_buf[512] = {0};

/* idle_state */
static int _idle_state_open(struct seq_file *s, void *data)
{
	return 0;
}

static int idle_state_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, _idle_state_open, inode->i_private);
}

static ssize_t idle_state_read(struct file *filp,
								char __user *userbuf,
								size_t count,
								loff_t *f_pos)
{
	int len = 0;
	char *p = dbg_buf;
	int i;

	p += sprintf(p, "********** idle state dump **********\n");

	for (i = 0; i < nr_cpu_ids; i++) {
		p += sprintf(p, "soidle_cnt[%d]=%lu, dpidle_cnt[%d]=%lu, slidle_cnt[%d]=%lu, rgidle_cnt[%d]=%lu\n",
				i, soidle_cnt[i], i, dpidle_cnt[i],
				i, slidle_cnt[i], i, rgidle_cnt[i]);
	}

	p += sprintf(p, "\n********** variables dump **********\n");
	for (i = 0; i < NR_TYPES; i++)
		p += sprintf(p, "%s_switch=%d, ", idle_name[i], idle_switch[i]);

	p += sprintf(p, "\n");

	p += sprintf(p, "\n********** idle command help **********\n");
	p += sprintf(p, "status help:   cat /sys/kernel/debug/cpuidle/idle_state\n");
	p += sprintf(p, "switch on/off: echo switch mask > /sys/kernel/debug/cpuidle/idle_state\n");

	p += sprintf(p, "soidle help:   cat /sys/kernel/debug/cpuidle/soidle_state\n");
	p += sprintf(p, "dpidle help:   cat /sys/kernel/debug/cpuidle/dpidle_state\n");
	p += sprintf(p, "slidle help:   cat /sys/kernel/debug/cpuidle/slidle_state\n");
	p += sprintf(p, "rgidle help:   cat /sys/kernel/debug/cpuidle/rgidle_state\n");

	len = p - dbg_buf;

	return simple_read_from_buffer(userbuf, count, f_pos, dbg_buf, len);
}

static ssize_t idle_state_write(struct file *filp,
								const char __user *userbuf,
								size_t count,
								loff_t *f_pos)
{
	char cmd[32];
	int idx;
	int param;

	count = min(count, sizeof(cmd_buf) - 1);

	if (copy_from_user(cmd_buf, userbuf, count))
		return -EFAULT;

	cmd_buf[count] = '\0';

	if (sscanf(cmd_buf, "%s %x", cmd, &param) == 2) {
		if (!strcmp(cmd, "switch")) {
			for (idx = 0; idx < NR_TYPES; idx++)
				idle_switch[idx] = (param & (1U << idx)) ? 1 : 0;
		}
		return count;
	}

	return -EINVAL;
}

static const struct file_operations idle_state_fops = {
	.open = idle_state_open,
	.read = idle_state_read,
	.write = idle_state_write,
	.llseek = seq_lseek,
	.release = single_release,
};
/* dpidle_state */
static int _dpidle_state_open(struct seq_file *s, void *data)
{
	return 0;
}

static int dpidle_state_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, _dpidle_state_open, inode->i_private);
}

static ssize_t dpidle_state_read(struct file *filp, char __user *userbuf, size_t count, loff_t *f_pos)
{
	int len = 0;
	char *p = dbg_buf;
	int i;

	p += sprintf(p, "*********** deep idle state ************\n");
	p += sprintf(p, "dpidle_time_critera=%u\n", dpidle_time_critera);

	for (i = 0; i < NR_REASONS; i++) {
		p += sprintf(p, "[%d]dpidle_block_cnt[%s]=%lu\n", i, reason_name[i],
				dpidle_block_cnt[i]);
	}

	p += sprintf(p, "\n");

	for (i = 0; i < NR_GRPS; i++) {
		p += sprintf(p, "[%02d]dpidle_condition_mask[%-8s]=0x%08x\t\tdpidle_block_mask[%-8s]=0x%08x\n", i,
				cg_grp_get_name(i), dpidle_condition_mask[i],
				cg_grp_get_name(i), dpidle_block_mask[i]);
	}

	p += sprintf(p, "dpidle_bypass_cg=%u\n", dpidle_by_pass_cg);

	p += sprintf(p, "\n*********** dpidle command help  ************\n");
	p += sprintf(p, "dpidle help:   cat /sys/kernel/debug/cpuidle/dpidle_state\n");
	p += sprintf(p, "switch on/off: echo [dpidle] 1/0 > /sys/kernel/debug/cpuidle/dpidle_state\n");
	p += sprintf(p, "cpupdn on/off: echo cpupdn 1/0 > /sys/kernel/debug/cpuidle/dpidle_state\n");
	p += sprintf(p, "en_dp_by_bit:  echo enable id > /sys/kernel/debug/cpuidle/dpidle_state\n");
	p += sprintf(p, "dis_dp_by_bit: echo disable id > /sys/kernel/debug/cpuidle/dpidle_state\n");
	p += sprintf(p, "modify tm_cri: echo time value(dec) > /sys/kernel/debug/cpuidle/dpidle_state\n");
	p += sprintf(p, "bypass cg:     echo bypass 1/0 > /sys/kernel/debug/cpuidle/dpidle_state\n");

	len = p - dbg_buf;

	return simple_read_from_buffer(userbuf, count, f_pos, dbg_buf, len);
}

static ssize_t dpidle_state_write(struct file *filp,
									const char __user *userbuf,
									size_t count,
									loff_t *f_pos)
{
	char cmd[32];
	int param;

	count = min(count, sizeof(cmd_buf) - 1);

	if (copy_from_user(cmd_buf, userbuf, count))
		return -EFAULT;

	cmd_buf[count] = '\0';

	if (sscanf(cmd_buf, "%s %d", cmd, &param) == 2) {
		if (!strcmp(cmd, "dpidle"))
			idle_switch[IDLE_TYPE_DP] = param;
		else if (!strcmp(cmd, "enable"))
			enable_dpidle_by_bit(param);
		else if (!strcmp(cmd, "disable"))
			disable_dpidle_by_bit(param);
		else if (!strcmp(cmd, "time"))
			dpidle_time_critera = param;
		else if (!strcmp(cmd, "bypass")) {
			dpidle_by_pass_cg = param;
			idle_dbg("bypass = %d\n", dpidle_by_pass_cg);
		}
		return count;
	} else if (sscanf(cmd_buf, "%d", &param) == 1) {
		idle_switch[IDLE_TYPE_DP] = param;
		return count;
	}

	return -EINVAL;
}

static const struct file_operations dpidle_state_fops = {
	.open = dpidle_state_open,
	.read = dpidle_state_read,
	.write = dpidle_state_write,
	.llseek = seq_lseek,
	.release = single_release,
};

/* soidle_state */
static int _soidle_state_open(struct seq_file *s, void *data)
{
	return 0;
}

static int soidle_state_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, _soidle_state_open, inode->i_private);
}

static ssize_t soidle_state_read(struct file *filp, char __user *userbuf, size_t count, loff_t *f_pos)
{
	int len = 0;
	char *p = dbg_buf;
	int i;

	p += sprintf(p, "*********** deep idle state ************\n");
	p += sprintf(p, "soidle_time_critera=%u\n", soidle_time_critera);

	for (i = 0; i < NR_REASONS; i++) {
		p += sprintf(p, "[%d]soidle_block_cnt[%s]=%lu\n", i, reason_name[i],
						soidle_block_cnt[0][i]);
	}

	p += sprintf(p, "\n");

	for (i = 0; i < NR_GRPS; i++) {
		p += sprintf(p, "[%02d]soidle_condition_mask[%-8s]=0x%08x\t\tsoidle_block_mask[%-8s]=0x%08x\n", i,
				cg_grp_get_name(i), soidle_condition_mask[i],
				cg_grp_get_name(i), soidle_block_mask[i]);
	}

	p += sprintf(p, "soidle_bypass_cg=%u\n", soidle_by_pass_cg);

	p += sprintf(p, "\n*********** soidle command help  ************\n");
	p += sprintf(p, "soidle help:   cat /sys/kernel/debug/cpuidle/soidle_state\n");
	p += sprintf(p, "switch on/off: echo [soidle] 1/0 > /sys/kernel/debug/cpuidle/soidle_state\n");
	p += sprintf(p, "cpupdn on/off: echo cpupdn 1/0 > /sys/kernel/debug/cpuidle/soidle_state\n");
	p += sprintf(p, "en_dp_by_bit:  echo enable id > /sys/kernel/debug/cpuidle/soidle_state\n");
	p += sprintf(p, "dis_dp_by_bit: echo disable id > /sys/kernel/debug/cpuidle/soidle_state\n");
	p += sprintf(p, "modify tm_cri: echo time value(dec) > /sys/kernel/debug/cpuidle/soidle_state\n");
	p += sprintf(p, "bypass cg:     echo bypass 1/0 > /sys/kernel/debug/cpuidle/soidle_state\n");

	len = p - dbg_buf;

	return simple_read_from_buffer(userbuf, count, f_pos, dbg_buf, len);
}

static ssize_t soidle_state_write(struct file *filp,
									const char __user *userbuf,
									size_t count,
									loff_t *f_pos)
{
	char cmd[32];
	int param;

	count = min(count, sizeof(cmd_buf) - 1);

	if (copy_from_user(cmd_buf, userbuf, count))
		return -EFAULT;

	cmd_buf[count] = '\0';

	if (sscanf(cmd_buf, "%s %d", cmd, &param) == 2) {
		if (!strcmp(cmd, "soidle"))
			idle_switch[IDLE_TYPE_SO] = param;
		else if (!strcmp(cmd, "enable"))
			enable_soidle_by_bit(param);
		else if (!strcmp(cmd, "disable"))
			disable_soidle_by_bit(param);
		else if (!strcmp(cmd, "time"))
			soidle_time_critera = param;
		else if (!strcmp(cmd, "bypass")) {
			soidle_by_pass_cg = param;
			idle_dbg("bypass = %d\n", soidle_by_pass_cg);
		}
		return count;
	} else if (sscanf(cmd_buf, "%d", &param) == 1) {
		idle_switch[IDLE_TYPE_SO] = param;
		return count;
	}

	return -EINVAL;
}

static const struct file_operations soidle_state_fops = {
	.open = soidle_state_open,
	.read = soidle_state_read,
	.write = soidle_state_write,
	.llseek = seq_lseek,
	.release = single_release,
};

/* slidle_state */
static int _slidle_state_open(struct seq_file *s, void *data)
{
	return 0;
}

static int slidle_state_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, _slidle_state_open, inode->i_private);
}

static ssize_t slidle_state_read(struct file *filp, char __user *userbuf, size_t count, loff_t *f_pos)
{
	int len = 0;
	char *p = dbg_buf;
	int i;

	p += sprintf(p, "*********** slow idle state ************\n");
	for (i = 0; i < NR_REASONS; i++) {
		p += sprintf(p, "[%d]slidle_block_cnt[%s]=%lu\n",
				i, reason_name[i], slidle_block_cnt[i]);
	}

	p += sprintf(p, "\n");

	for (i = 0; i < NR_GRPS; i++) {
		p += sprintf(p, "[%02d]slidle_condition_mask[%-8s]=0x%08x\t\tslidle_block_mask[%-8s]=0x%08x\n", i,
				cg_grp_get_name(i), slidle_condition_mask[i],
				cg_grp_get_name(i), slidle_block_mask[i]);
	}

	p += sprintf(p, "\n********** slidle command help **********\n");
	p += sprintf(p, "slidle help:   cat /sys/kernel/debug/cpuidle/slidle_state\n");
	p += sprintf(p, "switch on/off: echo [slidle] 1/0 > /sys/kernel/debug/cpuidle/slidle_state\n");

	len = p - dbg_buf;

	return simple_read_from_buffer(userbuf, count, f_pos, dbg_buf, len);
}

static ssize_t slidle_state_write(struct file *filp, const char __user *userbuf,
									size_t count, loff_t *f_pos)
{
	char cmd[32];
	int param;

	count = min(count, sizeof(cmd_buf) - 1);

	if (copy_from_user(cmd_buf, userbuf, count))
		return -EFAULT;

	cmd_buf[count] = '\0';

	if (sscanf(userbuf, "%s %d", cmd, &param) == 2) {
		if (!strcmp(cmd, "slidle"))
			idle_switch[IDLE_TYPE_SL] = param;
		else if (!strcmp(cmd, "enable"))
			enable_slidle_by_bit(param);
		else if (!strcmp(cmd, "disable"))
			disable_slidle_by_bit(param);

		return count;
	} else if (sscanf(userbuf, "%d", &param) == 1) {
		idle_switch[IDLE_TYPE_SL] = param;
		return count;
	}

	return -EINVAL;
}

static const struct file_operations slidle_state_fops = {
	.open = slidle_state_open,
	.read = slidle_state_read,
	.write = slidle_state_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *root_entry;

static int mt_cpuidle_debugfs_init(void)
{
	/* TODO: check if debugfs_create_file() failed */
	/* Initialize debugfs */
	root_entry = debugfs_create_dir("cpuidle", NULL);
	if (!root_entry) {
		idle_err("Can not create debugfs `dpidle_state`\n");
		return 1;
	}

	debugfs_create_file("idle_state", 0644, root_entry, NULL, &idle_state_fops);
	debugfs_create_file("dpidle_state", 0644, root_entry, NULL, &dpidle_state_fops);
	debugfs_create_file("soidle_state", 0644, root_entry, NULL, &soidle_state_fops);
	debugfs_create_file("slidle_state", 0644, root_entry, NULL,
			&slidle_state_fops);

	return 0;
}

#define idle_readl(addr)		DRV_Reg32(addr)
#define clk_readl(addr)			DRV_Reg32(addr)

#define clk_writel(addr, val)	mt_reg_sync_writel(val, addr)

static void __iomem *infrasys_base;
static void __iomem *perisys_base;
static void __iomem *audiosys_base;
static void __iomem *mfgsys_base;
static void __iomem *mmsys_base;
static void __iomem *imgsys_base;
static void __iomem *vdecsys_base;
static void __iomem *vencsys_base;
static void __iomem *sleepsys_base;
static void __iomem *cksys_base;

#define INFRA_REG(ofs)      (infrasys_base + ofs)
#define PREI_REG(ofs)       (perisys_base + ofs)
#define AUDIO_REG(ofs)      (audiosys_base + ofs)
#define MFG_REG(ofs)        (mfgsys_base + ofs)
#define MM_REG(ofs)         (mmsys_base + ofs)
#define IMG_REG(ofs)        (imgsys_base + ofs)
#define VDEC_REG(ofs)       (vdecsys_base + ofs)
#define VENC_REG(ofs)       (vencsys_base + ofs)
#define SPM_REG(ofs)        (sleepsys_base + ofs)
#define CKSYS_REG(ofs)      (cksys_base + ofs)

#ifdef SPM_PWR_STATUS
#undef SPM_PWR_STATUS
#endif

#ifdef SPM_PWR_STATUS_2ND
#undef SPM_PWR_STATUS_2ND
#endif

#define INFRA_PDN_STA       INFRA_REG(0x0048)
#define PERI_PDN0_STA       PREI_REG(0x0018)
#define PERI_PDN1_STA       PREI_REG(0x001C)
#define AUDIO_TOP_CON0      AUDIO_REG(0x0000)
#define MFG_CG_CON      MFG_REG(0)
#define DISP_CG_CON0        MM_REG(0x100)
#define DISP_CG_CON1        MM_REG(0x110)
#define IMG_CG_CON      IMG_REG(0x0000)
#define VDEC_CKEN_SET       VDEC_REG(0x0000)
#define LARB_CKEN_SET       VDEC_REG(0x0008)
#define VENC_CG_CON     VENC_REG(0x0)

#define SPM_PWR_STATUS      SPM_REG(0x060c)
#define SPM_PWR_STATUS_2ND  SPM_REG(0x0610)

#define CLK_CFG_4               CKSYS_REG(0x080)

#define DIS_PWR_STA_MASK        BIT(3)
#define MFG_PWR_STA_MASK        BIT(4)
#define ISP_PWR_STA_MASK        BIT(5)
#define VDE_PWR_STA_MASK        BIT(7)
#define VEN_PWR_STA_MASK        BIT(8)

enum subsys_id {
	SYS_VDE,
	SYS_MFG,
	SYS_VEN,
	SYS_ISP,
	SYS_DIS,
	NR_SYSS__,
};

static int sys_is_on(enum subsys_id id)
{
	u32 pwr_sta_mask[] = {
		VDE_PWR_STA_MASK,
		MFG_PWR_STA_MASK,
		VEN_PWR_STA_MASK,
		ISP_PWR_STA_MASK,
		DIS_PWR_STA_MASK,
	};

	u32 mask = pwr_sta_mask[id];
	u32 sta = idle_readl(SPM_PWR_STATUS);
	u32 sta_s = idle_readl(SPM_PWR_STATUS_2ND);

	return (sta & mask) && (sta_s & mask);
}

static void get_all_clock_state(u32 clks[NR_GRPS])
{
	int i;

	for (i = 0; i < NR_GRPS; i++)
		clks[i] = 0;

	clks[CG_INFRA] = ~idle_readl(INFRA_PDN_STA);       /* INFRA */

	clks[CG_PERI] = ~idle_readl(PERI_PDN0_STA);       /* PERI */

	if (sys_is_on(SYS_DIS)) {
		clks[CG_DISP0] = ~idle_readl(DISP_CG_CON0);    /* DISP0 */
		clks[CG_DISP1] = ~idle_readl(DISP_CG_CON1);    /* DISP1 */
	}

	if (sys_is_on(SYS_ISP))
		clks[CG_IMAGE] = ~idle_readl(IMG_CG_CON);  /* IMAGE */

	if (sys_is_on(SYS_MFG))
		clks[CG_MFG] = ~idle_readl(MFG_CG_CON);  /* MFG */

	clks[CG_AUDIO] = ~idle_readl(AUDIO_TOP_CON0);  /* AUDIO */

	if (sys_is_on(SYS_VDE)) {
		clks[CG_VDEC0] = idle_readl(VDEC_CKEN_SET);    /* VDEC0 */
		clks[CG_VDEC1] = idle_readl(LARB_CKEN_SET);    /* VDEC1 */
	}

	if (sys_is_on(SYS_VEN))
		clks[CG_VENC] = idle_readl(VENC_CG_CON); /* VENC_JPEG */
}

bool cg_check_idle_can_enter(
	unsigned int *condition_mask, unsigned int *block_mask)
{
	int i;
	unsigned int sd_mask = 0;
	u32 clks[NR_GRPS];
	u32 r = 0;

	msdc_clk_status(&sd_mask);
	if (sd_mask) {
		block_mask[CG_PERI] |= sd_mask;
		return false;
	}

	get_all_clock_state(clks);

	for (i = 0; i < NR_GRPS; i++) {
		block_mask[i] = condition_mask[i] & clks[i];
		r |= block_mask[i];
	}

	return r == 0;
}

static unsigned int clk_cfg_4;
void faudintbus_pll2sq(void)
{
	clk_cfg_4 = clk_readl(CLK_CFG_4);
	clk_writel(CLK_CFG_4, clk_cfg_4 & 0xFFFFFCFF);
	return;
}

void faudintbus_sq2pll(void)
{
	clk_writel(CLK_CFG_4, clk_cfg_4);
	return;
}

static int __init get_base_from_node(
	const char *cmp, void __iomem **pbase, int idx)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, cmp);

	if (!node) {
		idle_err("node '%s' not found!\n", cmp);
		return -1;
	}

	*pbase = of_iomap(node, idx);

	return 0;
}

static void __init iomap_init(void)
{
	get_base_from_node("mediatek,INFRACFG_AO", &infrasys_base, 0);
	get_base_from_node("mediatek,PERICFG", &perisys_base, 0);
	get_base_from_node("mediatek,AUDIO", &audiosys_base, 0);
	get_base_from_node("mediatek,G3D_CONFIG", &mfgsys_base, 0);
	get_base_from_node("mediatek,MMSYS_CONFIG", &mmsys_base, 0);
	get_base_from_node("mediatek,IMGSYS", &imgsys_base, 0);
	get_base_from_node("mediatek,VDEC_GCON", &vdecsys_base, 0);
	get_base_from_node("mediatek,VENC_GCON", &vencsys_base, 0);
	get_base_from_node("mediatek,SLEEP", &sleepsys_base, 0);
	get_base_from_node("mediatek,CKSYS", &cksys_base, 0);
}

void mt_cpuidle_framework_init(void)
{
    int err = 0;
    int i = 0;

	idle_ver("[%s]entry!!\n", __func__);

	err = request_gpt(idle_gpt, GPT_ONE_SHOT, GPT_CLK_SRC_SYS, GPT_CLK_DIV_1,
                0, NULL, GPT_NOAUTOEN);
    if (err) {
        idle_info("[%s]fail to request GPT%d\n", __func__,idle_gpt+1);
    }

    err = 0;

    for(i=0;i<NR_CPUS;i++){
        err |= cpu_xgpt_register_timer(i,NULL);
    }

    if (err) {
        idle_info("[%s]fail to request cpuxgpt\n", __func__);
    }

	iomap_init();
	mt_cpuidle_debugfs_init();
}
EXPORT_SYMBOL(mt_cpuidle_framework_init);

module_param(mt_idle_chk_golden, bool, 0644);
module_param(mt_dpidle_chk_golden, bool, 0644);
