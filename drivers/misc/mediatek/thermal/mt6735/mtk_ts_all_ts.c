#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/aee.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include "mach/mtk_thermal_monitor.h"
#include "mach/mt_typedefs.h"
#include "mach/mt_thermal.h"

#define MTK_ALLTS_SW_FILTER         (1)

extern struct proc_dir_entry * mtk_thermal_get_proc_drv_therm_dir_entry(void);

static unsigned int interval = 1000; /* mseconds, 0 : no auto polling */
static int trip_temp[10] = {120000,110000,100000,90000,80000,70000,65000,60000,55000,50000};

static unsigned int cl_dev_sysrst_state = 0;
static struct thermal_zone_device *thz_dev1;
static struct thermal_zone_device *thz_dev2;
static struct thermal_zone_device *thz_dev3;
//static struct thermal_zone_device *thz_dev4;

static struct thermal_cooling_device *cl_dev_sysrst1;
static struct thermal_cooling_device *cl_dev_sysrst2;
static struct thermal_cooling_device *cl_dev_sysrst3;
//static struct thermal_cooling_device *cl_dev_sysrst4;

static int tsallts_debug_log = 0;
static int kernelmode = 0;
static int g_THERMAL_TRIP[10] = {0,0,0,0,0,0,0,0,0,0};
static int num_trip=0;
static char g_bind0[20]={0};
static char g_bind1[20]={0};
static char g_bind2[20]={0};
static char g_bind3[20]={0};
static char g_bind4[20]={0};
static char g_bind5[20]={0};
static char g_bind6[20]={0};
static char g_bind7[20]={0};
static char g_bind8[20]={0};
static char g_bind9[20]={0};

#define TSALLTS_TEMP_CRIT 120000 /* 120.000 degree Celsius */


#define tsallts_dprintk(fmt, args...)   \
do {                                    \
    if (tsallts_debug_log) {                \
        pr_debug("[Power/ALLTS_Thermal]" fmt, ##args); \
    }                                   \
} while(0)



#define tsallts_printk(fmt, args...)   \
do {                                    \
	pr_debug("[Power/ALLTS_Thermal]" fmt, ##args); \
} while(0)


extern int get_immediate_temp2_wrap(void);
extern void mtkts_dump_cali_info(void);

static int tsallts_get_ts1_temp(void)
{
	int t_ret = 0;

	t_ret = get_immediate_ts1_wrap();
	tsallts_dprintk("[tsallts_get_ts1_temp] TS1 =%d\n", t_ret);
	return t_ret;
}
static int tsallts_get_ts2_temp(void)
{
	int t_ret = 0;

	t_ret = get_immediate_ts2_wrap();
	tsallts_dprintk("[tsallts_get_ts2_temp] TS5 =%d\n", t_ret);
	return t_ret;
}
static int tsallts_get_ts3_temp(void)
{
	int t_ret = 0;

	t_ret = get_immediate_ts3_wrap();
	tsallts_dprintk("[tsallts_get_ts3_temp] TS3(MCU1) =%d\n", t_ret);
	return t_ret;
}
#if 0
static int tsallts_get_ts4_temp(void)
{
	int t_ret = 0;

	t_ret = get_immediate_ts4_wrap();
	tsallts_dprintk("[tsallts_get_ts4_temp] TS4(MCU2) =%d\n", t_ret);
	return t_ret;
}
#endif
static int tsallts_get_temp1(struct thermal_zone_device *thermal,
			       unsigned long *t)
{
#if MTK_ALLTS_SW_FILTER == 1
	int curr_temp;
    int temp_temp;
    int ret = 0;
    static int last_abb_read_temp = 0;

	curr_temp = tsallts_get_ts1_temp();
    tsallts_dprintk("tsallts_get_temp1 CPU TS1(MCU4)=%d\n", curr_temp);

    if ((curr_temp > (trip_temp[0] - 15000)) || (curr_temp < -30000) || (curr_temp > 85000)) // abnormal high temp
        tsallts_printk("TS1(MCU1) =%d\n", curr_temp);

	temp_temp = curr_temp;
	if (curr_temp != 0) // not the first temp read after resume from suspension
	{
		if ((curr_temp > 150000) || (curr_temp < -20000)) // invalid range
		{
			tsallts_printk("TS1(MCU1) temp invalid=%d\n", curr_temp);
			temp_temp = 50000;
			ret = -1;
		}
	    else if (last_abb_read_temp != 0)
	    {
	    	if ((curr_temp - last_abb_read_temp > 20000) ||(last_abb_read_temp - curr_temp > 20000)) //delta 20C, invalid change
	    	{
	      		tsallts_printk("TS1(MCU1) temp float hugely temp=%d, lasttemp=%d\n", curr_temp, last_abb_read_temp);
				temp_temp = 50000;
				ret = -1;
	       	}
    	}
	}

	last_abb_read_temp = curr_temp;
	curr_temp = temp_temp;
	*t = (unsigned long) curr_temp;
	return ret;
#else
	int curr_temp;
	curr_temp = tsallts_get_ts1_temp();
    tsallts_dprintk("tsallts_get_temp TS1(MCU1)=%d\n", curr_temp);

    if ((curr_temp > (trip_temp[0] - 15000)) || (curr_temp < -30000))
        tsallts_printk("TS1(MCU1)=%d\n", curr_temp);

    *t  = curr_temp;

	return 0;
#endif
}

static int tsallts_get_temp2(struct thermal_zone_device *thermal,
			       unsigned long *t)
{
#if MTK_ALLTS_SW_FILTER == 1
	int curr_temp;
    int temp_temp;
    int ret = 0;
    static int last_abb_read_temp = 0;

	curr_temp = tsallts_get_ts2_temp();
    tsallts_dprintk("tsallts_get_temp2 TS2(MCU2)=%d\n", curr_temp);

    if ((curr_temp > (trip_temp[0] - 15000)) || (curr_temp < -30000) || (curr_temp > 85000)) // abnormal high temp
        tsallts_printk("T5(MCU2)=%d\n", curr_temp);

	temp_temp = curr_temp;
	if (curr_temp != 0) // not the first temp read after resume from suspension
	{
		if ((curr_temp > 150000) || (curr_temp < -20000)) // invalid range
		{
			tsallts_printk("TS2(MCU2) temp invalid=%d\n", curr_temp);
			temp_temp = 50000;
			ret = -1;
		}
	    else if (last_abb_read_temp != 0)
	    {
	    	if ((curr_temp - last_abb_read_temp > 20000) ||(last_abb_read_temp - curr_temp > 20000)) //delta 20C, invalid change
	    	{
	      		tsallts_printk("TS2(MCU2) temp float hugely temp=%d, lasttemp=%d\n", curr_temp, last_abb_read_temp);
				temp_temp = 50000;
				ret = -1;
	       	}
    	}
	}

	last_abb_read_temp = curr_temp;
	curr_temp = temp_temp;
	*t = (unsigned long) curr_temp;
	return ret;
#else
	int curr_temp;
	curr_temp = tsallts_get_ts2_temp();
    tsallts_dprintk("tsallts_get_temp T5(MCU3)=%d\n", curr_temp);

    if ((curr_temp > (trip_temp[0] - 15000)) || (curr_temp < -30000))
        tsallts_printk("T5(MCU3)=%d\n", curr_temp);

    *t  = curr_temp;

	return 0;
#endif
}

static int tsallts_get_temp3(struct thermal_zone_device *thermal,
			       unsigned long *t)
{
#if MTK_ALLTS_SW_FILTER == 1
	int curr_temp;
    int temp_temp;
    int ret = 0;
    static int last_abb_read_temp = 0;

	curr_temp = tsallts_get_ts3_temp();
    tsallts_dprintk("tsallts_get_temp3 TS3(MCU1)=%d\n", curr_temp);

    if ((curr_temp > (trip_temp[0] - 15000)) || (curr_temp < -30000) || (curr_temp > 85000)) // abnormal high temp
        tsallts_printk("TS3(MCU1)=%d\n", curr_temp);

	temp_temp = curr_temp;
	if (curr_temp != 0) // not the first temp read after resume from suspension
	{
		if ((curr_temp > 150000) || (curr_temp < -20000)) // invalid range
		{
			tsallts_printk("TS3(MCU1) temp invalid=%d\n", curr_temp);
			temp_temp = 50000;
			ret = -1;
		}
	    else if (last_abb_read_temp != 0)
	    {
	    	if ((curr_temp - last_abb_read_temp > 20000) ||(last_abb_read_temp - curr_temp > 20000)) //delta 20C, invalid change
	    	{
	      		tsallts_printk("TS3(MCU1) temp float hugely temp=%d, lasttemp=%d\n", curr_temp, last_abb_read_temp);
				temp_temp = 50000;
				ret = -1;
	       	}
    	}
	}

	last_abb_read_temp = curr_temp;
	curr_temp = temp_temp;
	*t = (unsigned long) curr_temp;
	return ret;
#else
	int curr_temp;
	curr_temp = tsallts_get_ts3_temp();
    tsallts_dprintk(" tsallts_get_temp TS3(MCU1)=%d\n", curr_temp);

    if ((curr_temp > (trip_temp[0] - 15000)) || (curr_temp < -30000))
        tsallts_printk("TS3(MCU1)=%d\n", curr_temp);

    *t  = curr_temp;

	return 0;
#endif
}

#if 0
static int tsallts_get_temp4(struct thermal_zone_device *thermal,
			       unsigned long *t)
{
#if MTK_ALLTS_SW_FILTER == 1
	int curr_temp;
    int temp_temp;
    int ret = 0;
    static int last_abb_read_temp = 0;

	curr_temp = tsallts_get_ts4_temp();
    tsallts_dprintk("tsallts_get_temp4 TS4(MCU2)=%d\n", curr_temp);

    if ((curr_temp > (trip_temp[0] - 15000)) || (curr_temp < -30000) || (curr_temp > 85000)) // abnormal high temp
        tsallts_printk("TS4(MCU2)=%d\n", curr_temp);

	temp_temp = curr_temp;
	if (curr_temp != 0) // not the first temp read after resume from suspension
	{
		if ((curr_temp > 150000) || (curr_temp < -20000)) // invalid range
		{
			tsallts_printk("TS4(MCU2) temp invalid=%d\n", curr_temp);
			temp_temp = 50000;
			ret = -1;
		}
	    else if (last_abb_read_temp != 0)
	    {
	    	if ((curr_temp - last_abb_read_temp > 20000) ||(last_abb_read_temp - curr_temp > 20000)) //delta 20C, invalid change
	    	{
	      		tsallts_printk("TS4(MCU2) temp float hugely temp=%d, lasttemp=%d\n", curr_temp, last_abb_read_temp);
				temp_temp = 50000;
				ret = -1;
	       	}
    	}
	}

	last_abb_read_temp = curr_temp;
	curr_temp = temp_temp;
	*t = (unsigned long) curr_temp;
	return ret;
#else
	int curr_temp;
	curr_temp = tsallts_get_ts4_temp();
    tsallts_dprintk("tsallts_get_temp TS4(MCU2)=%d\n", curr_temp);

    if ((curr_temp > (trip_temp[0] - 15000)) || (curr_temp < -30000))
        tsallts_printk("TS4(MCU2)=%d\n", curr_temp);

    *t  = curr_temp;

	return 0;
#endif
}
#endif

static int tsallts_bind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
    int table_val=0;
	tsallts_dprintk("[tsallts_bind] \n");
	if(!strcmp(cdev->type, g_bind0))
	{
		table_val = 0;
		tsallts_dprintk("[tsallts_bind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind1))
	{
		table_val = 1;
		tsallts_dprintk("[tsallts_bind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind2))
	{
		table_val = 2;
		tsallts_dprintk("[tsallts_bind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind3))
	{
		table_val = 3;
		tsallts_dprintk("[tsallts_bind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind4))
	{
		table_val = 4;
		tsallts_dprintk("[tsallts_bind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind5))
	{
		table_val = 5;
		tsallts_dprintk("[tsallts_bind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind6))
	{
		table_val = 6;
		tsallts_dprintk("[tsallts_bind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind7))
	{
		table_val = 7;
		tsallts_dprintk("[tsallts_bind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind8))
	{
		table_val = 8;
		tsallts_dprintk("[tsallts_bind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind9))
	{
		table_val = 9;
		tsallts_dprintk("[tsallts_bind] %s\n", cdev->type);
	}
	else
	{
		return 0;
	}

    if (mtk_thermal_zone_bind_cooling_device(thermal, table_val, cdev)) {
	    tsallts_dprintk("[tsallts_bind] error binding cooling dev\n");
		return -EINVAL;
	} else {
	    tsallts_dprintk("[tsallts_bind] binding OK, %d\n", table_val);
    }

	return 0;
}

static int tsallts_unbind(struct thermal_zone_device *thermal,
			  struct thermal_cooling_device *cdev)
{
	int table_val=0;
	tsallts_dprintk("[tsallts_unbind] \n");
	if(!strcmp(cdev->type, g_bind0))
	{
		table_val = 0;
		tsallts_dprintk("[tsallts_unbind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind1))
	{
		table_val = 1;
		tsallts_dprintk("[tsallts_unbind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind2))
	{
		table_val = 2;
		tsallts_dprintk("[tsallts_unbind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind3))
	{
		table_val = 3;
		tsallts_dprintk("[tsallts_unbind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind4))
	{
		table_val = 4;
		tsallts_dprintk("[tsallts_unbind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind5))
	{
		table_val = 5;
		tsallts_dprintk("[tsallts_unbind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind6))
	{
		table_val = 6;
		tsallts_dprintk("[tsallts_unbind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind7))
	{
		table_val = 7;
		tsallts_dprintk("[tsallts_unbind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind8))
	{
		table_val = 8;
		tsallts_dprintk("[tsallts_unbind] %s\n", cdev->type);
	}
	else if(!strcmp(cdev->type, g_bind9))
	{
		table_val = 9;
		tsallts_dprintk("[tsallts_unbind] %s\n", cdev->type);
	}
	else
		return 0;

	if (thermal_zone_unbind_cooling_device(thermal, table_val, cdev)) {
		tsallts_dprintk("[tsallts_unbind] error unbinding cooling dev\n");
		return -EINVAL;
	} else {
		tsallts_dprintk("[tsallts_unbind] unbinding OK\n");
	}

	return 0;
}

static int tsallts_get_mode(struct thermal_zone_device *thermal,
			    enum thermal_device_mode *mode)
{
	*mode = (kernelmode) ? THERMAL_DEVICE_ENABLED
		     : THERMAL_DEVICE_DISABLED;
	return 0;
}

static int tsallts_set_mode(struct thermal_zone_device *thermal,
			    enum thermal_device_mode mode)
{
	kernelmode = mode;
	return 0;
}

static int tsallts_get_trip_type(struct thermal_zone_device *thermal, int trip,
				 enum thermal_trip_type *type)
{
	*type = g_THERMAL_TRIP[trip];
	return 0;
}

static int tsallts_get_trip_temp(struct thermal_zone_device *thermal, int trip,
				 unsigned long *temp)
{
	*temp = trip_temp[trip];
	return 0;
}

static int tsallts_get_crit_temp(struct thermal_zone_device *thermal,
				 unsigned long *temperature)
{
	*temperature = TSALLTS_TEMP_CRIT;
	return 0;
}

/* bind callback functions to thermalzone */
static struct thermal_zone_device_ops tsallts_dev_ops1 = {
	.bind = tsallts_bind,
	.unbind = tsallts_unbind,
	.get_temp = tsallts_get_temp1,
	.get_mode = tsallts_get_mode,
	.set_mode = tsallts_set_mode,
	.get_trip_type = tsallts_get_trip_type,
	.get_trip_temp = tsallts_get_trip_temp,
	.get_crit_temp = tsallts_get_crit_temp,
};
/* bind callback functions to thermalzone */
static struct thermal_zone_device_ops tsallts_dev_ops2 = {
	.bind = tsallts_bind,
	.unbind = tsallts_unbind,
	.get_temp = tsallts_get_temp2,
	.get_mode = tsallts_get_mode,
	.set_mode = tsallts_set_mode,
	.get_trip_type = tsallts_get_trip_type,
	.get_trip_temp = tsallts_get_trip_temp,
	.get_crit_temp = tsallts_get_crit_temp,
};
/* bind callback functions to thermalzone */
static struct thermal_zone_device_ops tsallts_dev_ops3 = {
	.bind = tsallts_bind,
	.unbind = tsallts_unbind,
	.get_temp = tsallts_get_temp3,
	.get_mode = tsallts_get_mode,
	.set_mode = tsallts_set_mode,
	.get_trip_type = tsallts_get_trip_type,
	.get_trip_temp = tsallts_get_trip_temp,
	.get_crit_temp = tsallts_get_crit_temp,
};
#if 0
/* bind callback functions to thermalzone */
static struct thermal_zone_device_ops tsallts_dev_ops4 = {
	.bind = tsallts_bind,
	.unbind = tsallts_unbind,
	.get_temp = tsallts_get_temp4,
	.get_mode = tsallts_get_mode,
	.set_mode = tsallts_set_mode,
	.get_trip_type = tsallts_get_trip_type,
	.get_trip_temp = tsallts_get_trip_temp,
	.get_crit_temp = tsallts_get_crit_temp,
};
#endif

static int tsallts_sysrst_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = 1;
	return 0;
}
static int tsallts_sysrst_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = cl_dev_sysrst_state;
	return 0;
}
static int tsallts_sysrst_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
    cl_dev_sysrst_state = state;

    if(cl_dev_sysrst_state == 1)
	{
		mtkts_dump_cali_info();
		tsallts_printk("Power/ALL_Thermal: reset, reset, reset!!!");
		//tsallts_printk("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
		//tsallts_printk("*****************************************");
		//tsallts_printk("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");

//		BUG();
		 *(unsigned int*) 0x0 = 0xdead; // To trigger data abort to reset the system for thermal protection.
		//arch_reset(0,NULL);
	}
	return 0;
}

static struct thermal_cooling_device_ops tsallts_cooling_sysrst_ops1 = {
	.get_max_state = tsallts_sysrst_get_max_state,
	.get_cur_state = tsallts_sysrst_get_cur_state,
	.set_cur_state = tsallts_sysrst_set_cur_state,
};
static struct thermal_cooling_device_ops tsallts_cooling_sysrst_ops2 = {
	.get_max_state = tsallts_sysrst_get_max_state,
	.get_cur_state = tsallts_sysrst_get_cur_state,
	.set_cur_state = tsallts_sysrst_set_cur_state,
};
static struct thermal_cooling_device_ops tsallts_cooling_sysrst_ops3 = {
	.get_max_state = tsallts_sysrst_get_max_state,
	.get_cur_state = tsallts_sysrst_get_cur_state,
	.set_cur_state = tsallts_sysrst_set_cur_state,
};
#if 0
static struct thermal_cooling_device_ops tsallts_cooling_sysrst_ops4 = {
	.get_max_state = tsallts_sysrst_get_max_state,
	.get_cur_state = tsallts_sysrst_get_cur_state,
	.set_cur_state = tsallts_sysrst_set_cur_state,
};
#endif

static int tsallts_read1(struct seq_file *m, void *v)
{


tsallts_dprintk( "tsallts_read1\n");


	seq_printf(m, "[ tsallts_read1] trip_0_temp=%d,trip_1_temp=%d,trip_2_temp=%d,trip_3_temp=%d,trip_4_temp=%d,\n\
		trip_5_temp=%d,trip_6_temp=%d,trip_7_temp=%d,trip_8_temp=%d,trip_9_temp=%d,\n\
		g_THERMAL_TRIP_0=%d,g_THERMAL_TRIP_1=%d,g_THERMAL_TRIP_2=%d,g_THERMAL_TRIP_3=%d,g_THERMAL_TRIP_4=%d,\n\
		g_THERMAL_TRIP_5=%d,g_THERMAL_TRIP_6=%d,g_THERMAL_TRIP_7=%d,g_THERMAL_TRIP_8=%d,g_THERMAL_TRIP_9=%d,\n\
		cooldev0=%s,cooldev1=%s,cooldev2=%s,cooldev3=%s,cooldev4=%s,\n\
		cooldev5=%s,cooldev6=%s,cooldev7=%s,cooldev8=%s,cooldev9=%s,time_ms=%d\n",
		trip_temp[0],trip_temp[1],trip_temp[2],trip_temp[3],trip_temp[4],
		trip_temp[5],trip_temp[6],trip_temp[7],trip_temp[8],trip_temp[9],
		g_THERMAL_TRIP[0],g_THERMAL_TRIP[1],g_THERMAL_TRIP[2],g_THERMAL_TRIP[3],g_THERMAL_TRIP[4],
		g_THERMAL_TRIP[5],g_THERMAL_TRIP[6],g_THERMAL_TRIP[7],g_THERMAL_TRIP[8],g_THERMAL_TRIP[9],
		g_bind0,g_bind1,g_bind2,g_bind3,g_bind4,g_bind5,g_bind6,g_bind7,g_bind8,g_bind9,
		interval);

	return 0;
}


static int tsallts_read2(struct seq_file *m, void *v)
{


tsallts_dprintk( "tsallts_read2\n");


	seq_printf(m, "[ tsallts_read2] trip_0_temp=%d,trip_1_temp=%d,trip_2_temp=%d,trip_3_temp=%d,trip_4_temp=%d,\n\
		trip_5_temp=%d,trip_6_temp=%d,trip_7_temp=%d,trip_8_temp=%d,trip_9_temp=%d,\n\
		g_THERMAL_TRIP_0=%d,g_THERMAL_TRIP_1=%d,g_THERMAL_TRIP_2=%d,g_THERMAL_TRIP_3=%d,g_THERMAL_TRIP_4=%d,\n\
		g_THERMAL_TRIP_5=%d,g_THERMAL_TRIP_6=%d,g_THERMAL_TRIP_7=%d,g_THERMAL_TRIP_8=%d,g_THERMAL_TRIP_9=%d,\n\
		cooldev0=%s,cooldev1=%s,cooldev2=%s,cooldev3=%s,cooldev4=%s,\n\
		cooldev5=%s,cooldev6=%s,cooldev7=%s,cooldev8=%s,cooldev9=%s,time_ms=%d\n",
		trip_temp[0],trip_temp[1],trip_temp[2],trip_temp[3],trip_temp[4],
		trip_temp[5],trip_temp[6],trip_temp[7],trip_temp[8],trip_temp[9],
		g_THERMAL_TRIP[0],g_THERMAL_TRIP[1],g_THERMAL_TRIP[2],g_THERMAL_TRIP[3],g_THERMAL_TRIP[4],
		g_THERMAL_TRIP[5],g_THERMAL_TRIP[6],g_THERMAL_TRIP[7],g_THERMAL_TRIP[8],g_THERMAL_TRIP[9],
		g_bind0,g_bind1,g_bind2,g_bind3,g_bind4,g_bind5,g_bind6,g_bind7,g_bind8,g_bind9,
		interval);

	return 0;
}


static int tsallts_read3(struct seq_file *m, void *v)
{


tsallts_dprintk( "tsallts_read3\n");


	seq_printf(m, "[ tsallts_read3] trip_0_temp=%d,trip_1_temp=%d,trip_2_temp=%d,trip_3_temp=%d,trip_4_temp=%d,\n\
		trip_5_temp=%d,trip_6_temp=%d,trip_7_temp=%d,trip_8_temp=%d,trip_9_temp=%d,\n\
		g_THERMAL_TRIP_0=%d,g_THERMAL_TRIP_1=%d,g_THERMAL_TRIP_2=%d,g_THERMAL_TRIP_3=%d,g_THERMAL_TRIP_4=%d,\n\
		g_THERMAL_TRIP_5=%d,g_THERMAL_TRIP_6=%d,g_THERMAL_TRIP_7=%d,g_THERMAL_TRIP_8=%d,g_THERMAL_TRIP_9=%d,\n\
		cooldev0=%s,cooldev1=%s,cooldev2=%s,cooldev3=%s,cooldev4=%s,\n\
		cooldev5=%s,cooldev6=%s,cooldev7=%s,cooldev8=%s,cooldev9=%s,time_ms=%d\n",
		trip_temp[0],trip_temp[1],trip_temp[2],trip_temp[3],trip_temp[4],
		trip_temp[5],trip_temp[6],trip_temp[7],trip_temp[8],trip_temp[9],
		g_THERMAL_TRIP[0],g_THERMAL_TRIP[1],g_THERMAL_TRIP[2],g_THERMAL_TRIP[3],g_THERMAL_TRIP[4],
		g_THERMAL_TRIP[5],g_THERMAL_TRIP[6],g_THERMAL_TRIP[7],g_THERMAL_TRIP[8],g_THERMAL_TRIP[9],
		g_bind0,g_bind1,g_bind2,g_bind3,g_bind4,g_bind5,g_bind6,g_bind7,g_bind8,g_bind9,
		interval);

	return 0;
}


static int tsallts_read4(struct seq_file *m, void *v)
{


tsallts_dprintk( "tsallts_read4\n");


	seq_printf(m, "[ tsallts_read4] trip_0_temp=%d,trip_1_temp=%d,trip_2_temp=%d,trip_3_temp=%d,trip_4_temp=%d,\n\
		trip_5_temp=%d,trip_6_temp=%d,trip_7_temp=%d,trip_8_temp=%d,trip_9_temp=%d,\n\
		g_THERMAL_TRIP_0=%d,g_THERMAL_TRIP_1=%d,g_THERMAL_TRIP_2=%d,g_THERMAL_TRIP_3=%d,g_THERMAL_TRIP_4=%d,\n\
		g_THERMAL_TRIP_5=%d,g_THERMAL_TRIP_6=%d,g_THERMAL_TRIP_7=%d,g_THERMAL_TRIP_8=%d,g_THERMAL_TRIP_9=%d,\n\
		cooldev0=%s,cooldev1=%s,cooldev2=%s,cooldev3=%s,cooldev4=%s,\n\
		cooldev5=%s,cooldev6=%s,cooldev7=%s,cooldev8=%s,cooldev9=%s,time_ms=%d\n",
		trip_temp[0],trip_temp[1],trip_temp[2],trip_temp[3],trip_temp[4],
		trip_temp[5],trip_temp[6],trip_temp[7],trip_temp[8],trip_temp[9],
		g_THERMAL_TRIP[0],g_THERMAL_TRIP[1],g_THERMAL_TRIP[2],g_THERMAL_TRIP[3],g_THERMAL_TRIP[4],
		g_THERMAL_TRIP[5],g_THERMAL_TRIP[6],g_THERMAL_TRIP[7],g_THERMAL_TRIP[8],g_THERMAL_TRIP[9],
		g_bind0,g_bind1,g_bind2,g_bind3,g_bind4,g_bind5,g_bind6,g_bind7,g_bind8,g_bind9,
		interval);

	return 0;
}


int tsallts_register_thermal(void);
void tsallts_unregister_thermal(void);



static ssize_t tsallts_write1(struct file *file, const char __user *buffer, size_t count, loff_t *data)
{
	int len=0,time_msec=0;
	int trip[10]={0};
	int t_type[10]={0};
	int i;
	char bind0[20],bind1[20],bind2[20],bind3[20],bind4[20];
	char bind5[20],bind6[20],bind7[20],bind8[20],bind9[20];
	char desc[512];


	tsallts_printk("[tsallts_write1] \n");

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
	{
		return 0;
	}
	desc[len] = '\0';

	if (sscanf(desc, "%d %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d",
		&num_trip, &trip[0],&t_type[0],bind0, &trip[1],&t_type[1],bind1,
		&trip[2],&t_type[2],bind2, &trip[3],&t_type[3],bind3,
		&trip[4],&t_type[4],bind4, &trip[5],&t_type[5],bind5,
		&trip[6],&t_type[6],bind6, &trip[7],&t_type[7],bind7,
		&trip[8],&t_type[8],bind8, &trip[9],&t_type[9],bind9,
		&time_msec) == 32)
	{
		tsallts_dprintk("[tsallts_write1] tsallts_unregister_thermal\n");
		tsallts_unregister_thermal();


		if(num_trip<0) return -EINVAL;

		for(i=0; i<num_trip; i++)
			g_THERMAL_TRIP[i] = t_type[i];

		g_bind0[0]=g_bind1[0]=g_bind2[0]=g_bind3[0]=g_bind4[0]=g_bind5[0]=g_bind6[0]=g_bind7[0]=g_bind8[0]=g_bind9[0]='\0';

		for(i=0; i<20; i++)
		{
			g_bind0[i]=bind0[i];
			g_bind1[i]=bind1[i];
			g_bind2[i]=bind2[i];
			g_bind3[i]=bind3[i];
			g_bind4[i]=bind4[i];
			g_bind5[i]=bind5[i];
			g_bind6[i]=bind6[i];
			g_bind7[i]=bind7[i];
			g_bind8[i]=bind8[i];
			g_bind9[i]=bind9[i];
		}

		tsallts_dprintk("[tsallts_write1] g_THERMAL_TRIP_0=%d,g_THERMAL_TRIP_1=%d,g_THERMAL_TRIP_2=%d,g_THERMAL_TRIP_3=%d,g_THERMAL_TRIP_4=%d,\
		g_THERMAL_TRIP_5=%d,g_THERMAL_TRIP_6=%d,g_THERMAL_TRIP_7=%d,g_THERMAL_TRIP_8=%d,g_THERMAL_TRIP_9=%d,\n",
			g_THERMAL_TRIP[0],g_THERMAL_TRIP[1],g_THERMAL_TRIP[2],g_THERMAL_TRIP[3],g_THERMAL_TRIP[4],
			g_THERMAL_TRIP[5],g_THERMAL_TRIP[6],g_THERMAL_TRIP[7],g_THERMAL_TRIP[8],g_THERMAL_TRIP[9]);
		tsallts_dprintk("[tsallts_write1] cooldev0=%s,cooldev1=%s,cooldev2=%s,cooldev3=%s,cooldev4=%s,\
		cooldev5=%s,cooldev6=%s,cooldev7=%s,cooldev8=%s,cooldev9=%s\n",
			g_bind0,g_bind1,g_bind2,g_bind3,g_bind4,g_bind5,g_bind6,g_bind7,g_bind8,g_bind9);

		for(i=0; i<num_trip; i++)
		{
			trip_temp[i]=trip[i];
		}

		interval=time_msec;

		tsallts_dprintk("[tsallts_write] trip_0_temp=%d,trip_1_temp=%d,trip_2_temp=%d,trip_3_temp=%d,trip_4_temp=%d,\
		trip_5_temp=%d,trip_6_temp=%d,trip_7_temp=%d,trip_8_temp=%d,trip_9_temp=%d,time_ms=%d\n",
		trip_temp[0],trip_temp[1],trip_temp[2],trip_temp[3],trip_temp[4],
		trip_temp[5],trip_temp[6],trip_temp[7],trip_temp[8],trip_temp[9],interval);

		tsallts_dprintk("[tsallts_write1] tsallts_register_thermal\n");
		tsallts_register_thermal();

		return count;
	}
	else
	{
		tsallts_dprintk("[tsallts_write1] bad argument\n");
	}

	return -EINVAL;
}

static ssize_t tsallts_write2(struct file *file, const char __user *buffer, size_t count, loff_t *data)
{
	int len=0,time_msec=0;
	int trip[10]={0};
	int t_type[10]={0};
	int i;
	char bind0[20],bind1[20],bind2[20],bind3[20],bind4[20];
	char bind5[20],bind6[20],bind7[20],bind8[20],bind9[20];
	char desc[512];


	tsallts_printk("[tsallts_write2] \n");

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
	{
		return 0;
	}
	desc[len] = '\0';

	if (sscanf(desc, "%d %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d",
		&num_trip, &trip[0],&t_type[0],bind0, &trip[1],&t_type[1],bind1,
		&trip[2],&t_type[2],bind2, &trip[3],&t_type[3],bind3,
		&trip[4],&t_type[4],bind4, &trip[5],&t_type[5],bind5,
		&trip[6],&t_type[6],bind6, &trip[7],&t_type[7],bind7,
		&trip[8],&t_type[8],bind8, &trip[9],&t_type[9],bind9,
		&time_msec) == 32)
	{
		tsallts_dprintk("[tsallts_write2] tsallts_unregister_thermal\n");
		tsallts_unregister_thermal();

		if(num_trip<0) return -EINVAL;

		for(i=0; i<num_trip; i++)
			g_THERMAL_TRIP[i] = t_type[i];

		g_bind0[0]=g_bind1[0]=g_bind2[0]=g_bind3[0]=g_bind4[0]=g_bind5[0]=g_bind6[0]=g_bind7[0]=g_bind8[0]=g_bind9[0]='\0';

		for(i=0; i<20; i++)
		{
			g_bind0[i]=bind0[i];
			g_bind1[i]=bind1[i];
			g_bind2[i]=bind2[i];
			g_bind3[i]=bind3[i];
			g_bind4[i]=bind4[i];
			g_bind5[i]=bind5[i];
			g_bind6[i]=bind6[i];
			g_bind7[i]=bind7[i];
			g_bind8[i]=bind8[i];
			g_bind9[i]=bind9[i];
		}

		tsallts_dprintk("[tsallts_write2] g_THERMAL_TRIP_0=%d,g_THERMAL_TRIP_1=%d,g_THERMAL_TRIP_2=%d,g_THERMAL_TRIP_3=%d,g_THERMAL_TRIP_4=%d,\
		g_THERMAL_TRIP_5=%d,g_THERMAL_TRIP_6=%d,g_THERMAL_TRIP_7=%d,g_THERMAL_TRIP_8=%d,g_THERMAL_TRIP_9=%d,\n",
			g_THERMAL_TRIP[0],g_THERMAL_TRIP[1],g_THERMAL_TRIP[2],g_THERMAL_TRIP[3],g_THERMAL_TRIP[4],
			g_THERMAL_TRIP[5],g_THERMAL_TRIP[6],g_THERMAL_TRIP[7],g_THERMAL_TRIP[8],g_THERMAL_TRIP[9]);
		tsallts_dprintk("[tsallts_write5] cooldev0=%s,cooldev1=%s,cooldev2=%s,cooldev3=%s,cooldev4=%s,\
		cooldev5=%s,cooldev6=%s,cooldev7=%s,cooldev8=%s,cooldev9=%s\n",
			g_bind0,g_bind1,g_bind2,g_bind3,g_bind4,g_bind5,g_bind6,g_bind7,g_bind8,g_bind9);

		for(i=0; i<num_trip; i++)
		{
			trip_temp[i]=trip[i];
		}

		interval=time_msec;

		tsallts_dprintk("[tsallts_write2] trip_0_temp=%d,trip_1_temp=%d,trip_2_temp=%d,trip_3_temp=%d,trip_4_temp=%d,\
		trip_5_temp=%d,trip_6_temp=%d,trip_7_temp=%d,trip_8_temp=%d,trip_9_temp=%d,time_ms=%d\n",
		trip_temp[0],trip_temp[1],trip_temp[2],trip_temp[3],trip_temp[4],
		trip_temp[5],trip_temp[6],trip_temp[7],trip_temp[8],trip_temp[9],interval);

		tsallts_dprintk("[tsallts_write2] tsallts_register_thermal\n");
		tsallts_register_thermal();

		return count;
	}
	else
	{
		tsallts_dprintk("[tsallts_write2] bad argument\n");
	}

	return -EINVAL;
}

static ssize_t tsallts_write3(struct file *file, const char __user *buffer, size_t count, loff_t *data)
{
	int len=0,time_msec=0;
	int trip[10]={0};
	int t_type[10]={0};
	int i;
	char bind0[20],bind1[20],bind2[20],bind3[20],bind4[20];
	char bind5[20],bind6[20],bind7[20],bind8[20],bind9[20];
	char desc[512];


	tsallts_printk("[tsallts_write3] \n");

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
	{
		return 0;
	}
	desc[len] = '\0';

	if (sscanf(desc, "%d %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d",
		&num_trip, &trip[0],&t_type[0],bind0, &trip[1],&t_type[1],bind1,
		&trip[2],&t_type[2],bind2, &trip[3],&t_type[3],bind3,
		&trip[4],&t_type[4],bind4, &trip[5],&t_type[5],bind5,
		&trip[6],&t_type[6],bind6, &trip[7],&t_type[7],bind7,
		&trip[8],&t_type[8],bind8, &trip[9],&t_type[9],bind9,
		&time_msec) == 32)
	{
		tsallts_dprintk("[tsallts_write3] tsallts_unregister_thermal\n");
		tsallts_unregister_thermal();

		if(num_trip<0) return -EINVAL;

		for(i=0; i<num_trip; i++)
			g_THERMAL_TRIP[i] = t_type[i];

		g_bind0[0]=g_bind1[0]=g_bind2[0]=g_bind3[0]=g_bind4[0]=g_bind5[0]=g_bind6[0]=g_bind7[0]=g_bind8[0]=g_bind9[0]='\0';

		for(i=0; i<20; i++)
		{
			g_bind0[i]=bind0[i];
			g_bind1[i]=bind1[i];
			g_bind2[i]=bind2[i];
			g_bind3[i]=bind3[i];
			g_bind4[i]=bind4[i];
			g_bind5[i]=bind5[i];
			g_bind6[i]=bind6[i];
			g_bind7[i]=bind7[i];
			g_bind8[i]=bind8[i];
			g_bind9[i]=bind9[i];
		}

		tsallts_dprintk("[tsallts_write3] g_THERMAL_TRIP_0=%d,g_THERMAL_TRIP_1=%d,g_THERMAL_TRIP_2=%d,g_THERMAL_TRIP_3=%d,g_THERMAL_TRIP_4=%d,\
		g_THERMAL_TRIP_5=%d,g_THERMAL_TRIP_6=%d,g_THERMAL_TRIP_7=%d,g_THERMAL_TRIP_8=%d,g_THERMAL_TRIP_9=%d,\n",
			g_THERMAL_TRIP[0],g_THERMAL_TRIP[1],g_THERMAL_TRIP[2],g_THERMAL_TRIP[3],g_THERMAL_TRIP[4],
			g_THERMAL_TRIP[5],g_THERMAL_TRIP[6],g_THERMAL_TRIP[7],g_THERMAL_TRIP[8],g_THERMAL_TRIP[9]);
		tsallts_dprintk("[tsallts_write] cooldev0=%s,cooldev1=%s,cooldev2=%s,cooldev3=%s,cooldev4=%s,\
		cooldev5=%s,cooldev6=%s,cooldev7=%s,cooldev8=%s,cooldev9=%s\n",
			g_bind0,g_bind1,g_bind2,g_bind3,g_bind4,g_bind5,g_bind6,g_bind7,g_bind8,g_bind9);

		for(i=0; i<num_trip; i++)
		{
			trip_temp[i]=trip[i];
		}

		interval=time_msec;

		tsallts_dprintk("[tsallts_write3] trip_0_temp=%d,trip_1_temp=%d,trip_2_temp=%d,trip_3_temp=%d,trip_4_temp=%d,\
		trip_5_temp=%d,trip_6_temp=%d,trip_7_temp=%d,trip_8_temp=%d,trip_9_temp=%d,time_ms=%d\n",
		trip_temp[0],trip_temp[1],trip_temp[2],trip_temp[3],trip_temp[4],
		trip_temp[5],trip_temp[6],trip_temp[7],trip_temp[8],trip_temp[9],interval);

		tsallts_dprintk("[tsallts_write3] tsallts_register_thermal\n");
		tsallts_register_thermal();

		return count;
	}
	else
	{
		tsallts_dprintk("[tsallts_write3] bad argument\n");
	}

	return -EINVAL;
}

static ssize_t tsallts_write4(struct file *file, const char __user *buffer, size_t count, loff_t *data)
{
	int len=0,time_msec=0;
	int trip[10]={0};
	int t_type[10]={0};
	int i;
	char bind0[20],bind1[20],bind2[20],bind3[20],bind4[20];
	char bind5[20],bind6[20],bind7[20],bind8[20],bind9[20];
	char desc[512];


	tsallts_printk("[tsallts_write4] \n");

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
	{
		return 0;
	}
	desc[len] = '\0';

	if (sscanf(desc, "%d %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d %d %19s %d",
		&num_trip, &trip[0],&t_type[0],bind0, &trip[1],&t_type[1],bind1,
		&trip[2],&t_type[2],bind2, &trip[3],&t_type[3],bind3,
		&trip[4],&t_type[4],bind4, &trip[5],&t_type[5],bind5,
		&trip[6],&t_type[6],bind6, &trip[7],&t_type[7],bind7,
		&trip[8],&t_type[8],bind8, &trip[9],&t_type[9],bind9,
		&time_msec) == 32)
	{
		tsallts_dprintk("[tsallts_write4] tsallts_unregister_thermal\n");
		tsallts_unregister_thermal();

		if(num_trip<0) return -EINVAL;

		for(i=0; i<num_trip; i++)
			g_THERMAL_TRIP[i] = t_type[i];

		g_bind0[0]=g_bind1[0]=g_bind2[0]=g_bind3[0]=g_bind4[0]=g_bind5[0]=g_bind6[0]=g_bind7[0]=g_bind8[0]=g_bind9[0]='\0';

		for(i=0; i<20; i++)
		{
			g_bind0[i]=bind0[i];
			g_bind1[i]=bind1[i];
			g_bind2[i]=bind2[i];
			g_bind3[i]=bind3[i];
			g_bind4[i]=bind4[i];
			g_bind5[i]=bind5[i];
			g_bind6[i]=bind6[i];
			g_bind7[i]=bind7[i];
			g_bind8[i]=bind8[i];
			g_bind9[i]=bind9[i];
		}

		tsallts_dprintk("[tsallts_write4] g_THERMAL_TRIP_0=%d,g_THERMAL_TRIP_1=%d,g_THERMAL_TRIP_2=%d,g_THERMAL_TRIP_3=%d,g_THERMAL_TRIP_4=%d,\
		g_THERMAL_TRIP_5=%d,g_THERMAL_TRIP_6=%d,g_THERMAL_TRIP_7=%d,g_THERMAL_TRIP_8=%d,g_THERMAL_TRIP_9=%d,\n",
			g_THERMAL_TRIP[0],g_THERMAL_TRIP[1],g_THERMAL_TRIP[2],g_THERMAL_TRIP[3],g_THERMAL_TRIP[4],
			g_THERMAL_TRIP[5],g_THERMAL_TRIP[6],g_THERMAL_TRIP[7],g_THERMAL_TRIP[8],g_THERMAL_TRIP[9]);
		tsallts_dprintk("[tsallts_write] cooldev0=%s,cooldev1=%s,cooldev2=%s,cooldev3=%s,cooldev4=%s,\
		cooldev5=%s,cooldev6=%s,cooldev7=%s,cooldev8=%s,cooldev9=%s\n",
			g_bind0,g_bind1,g_bind2,g_bind3,g_bind4,g_bind5,g_bind6,g_bind7,g_bind8,g_bind9);

		for(i=0; i<num_trip; i++)
		{
			trip_temp[i]=trip[i];
		}

		interval=time_msec;

		tsallts_dprintk("[tsallts_write4] trip_0_temp=%d,trip_1_temp=%d,trip_2_temp=%d,trip_3_temp=%d,trip_4_temp=%d,\
		trip_5_temp=%d,trip_6_temp=%d,trip_7_temp=%d,trip_8_temp=%d,trip_9_temp=%d,time_ms=%d\n",
		trip_temp[0],trip_temp[1],trip_temp[2],trip_temp[3],trip_temp[4],
		trip_temp[5],trip_temp[6],trip_temp[7],trip_temp[8],trip_temp[9],interval);

		tsallts_dprintk("[tsallts_write4] tsallts_register_thermal\n");
		tsallts_register_thermal();

		return count;
	}
	else
	{
		tsallts_dprintk("[tsallts_write4] bad argument\n");
	}

	return -EINVAL;
}


void mtkts_allts_cancel_thermal_timer(void)
{
	//cancel timer
	//tsallts_printk("mtkts_btsmdpa_cancel_thermal_timer \n");
	// stop thermal framework polling when entering deep idle
	if (thz_dev1)
	    cancel_delayed_work(&(thz_dev1->poll_queue));
	if (thz_dev2)
	    cancel_delayed_work(&(thz_dev2->poll_queue));
	if (thz_dev3)
	    cancel_delayed_work(&(thz_dev3->poll_queue));

}


void mtkts_allts_start_thermal_timer(void)
{
	//tsallts_printk("mtkts_btsmdpa_start_thermal_timer \n");
    // resume thermal framework polling when leaving deep idle
    if (thz_dev1 != NULL && interval != 0)
	    mod_delayed_work(system_freezable_wq, &(thz_dev1->poll_queue), round_jiffies(msecs_to_jiffies(1000))); // 2sec
    if (thz_dev2 != NULL && interval != 0)
	    mod_delayed_work(system_freezable_wq, &(thz_dev2->poll_queue), round_jiffies(msecs_to_jiffies(1000))); // 2sec
    if (thz_dev3 != NULL && interval != 0)
	    mod_delayed_work(system_freezable_wq, &(thz_dev3->poll_queue), round_jiffies(msecs_to_jiffies(1000))); // 2sec

}



int tsallts_register_cooler(void)
{
    cl_dev_sysrst1 = mtk_thermal_cooling_device_register("mtkts1-sysrst", NULL,
                    &tsallts_cooling_sysrst_ops1);

    cl_dev_sysrst2 = mtk_thermal_cooling_device_register("mtkts2-sysrst", NULL,
                	&tsallts_cooling_sysrst_ops2);

    cl_dev_sysrst3 = mtk_thermal_cooling_device_register("mtkts3-sysrst", NULL,
            		&tsallts_cooling_sysrst_ops3);
#if 0
    cl_dev_sysrst4 = mtk_thermal_cooling_device_register("mtkts4-sysrst", NULL,
                	&tsallts_cooling_sysrst_ops4);
#endif
    return 0;
}

int tsallts_register_thermal(void)
{
    tsallts_dprintk("[tsallts_register_thermal] \n");

    /* trips : trip 0~3 */
    thz_dev1 = mtk_thermal_zone_device_register("mtkts1", num_trip, NULL,
                    &tsallts_dev_ops1, 0, 0, 0, interval);
    /* trips : trip 0~3 */
    thz_dev2 = mtk_thermal_zone_device_register("mtkts2", num_trip, NULL,
                    &tsallts_dev_ops2, 0, 0, 0, interval);
    /* trips : trip 0~3 */
    thz_dev3 = mtk_thermal_zone_device_register("mtkts3", num_trip, NULL,
                    &tsallts_dev_ops3, 0, 0, 0, interval);

#if 0
    /* trips : trip 0~3 */
    thz_dev4 = mtk_thermal_zone_device_register("mtkts4", num_trip, NULL,
                    &tsallts_dev_ops4, 0, 0, 0, interval);
#endif
    return 0;
}

void tsallts_unregister_cooler(void)
{
	if (cl_dev_sysrst1) {
		mtk_thermal_cooling_device_unregister(cl_dev_sysrst1);
		cl_dev_sysrst1 = NULL;
	}

	if (cl_dev_sysrst2) {
		mtk_thermal_cooling_device_unregister(cl_dev_sysrst2);
		cl_dev_sysrst2 = NULL;
	}
	if (cl_dev_sysrst3) {
		mtk_thermal_cooling_device_unregister(cl_dev_sysrst3);
		cl_dev_sysrst3 = NULL;
	}
#if 0
	if (cl_dev_sysrst4) {
		mtk_thermal_cooling_device_unregister(cl_dev_sysrst4);
		cl_dev_sysrst4 = NULL;
	}
#endif
}

void tsallts_unregister_thermal(void)
{
	tsallts_dprintk("[tsallts_unregister_thermal] \n");

	if (thz_dev1) {
		mtk_thermal_zone_device_unregister(thz_dev1);
		thz_dev1 = NULL;
	}

	if (thz_dev2) {
		mtk_thermal_zone_device_unregister(thz_dev2);
		thz_dev2 = NULL;
	}
	if (thz_dev3) {
		mtk_thermal_zone_device_unregister(thz_dev3);
		thz_dev3 = NULL;
	}
#if 0
	if (thz_dev4) {
		mtk_thermal_zone_device_unregister(thz_dev4);
		thz_dev4 = NULL;
	}
#endif
}

static int tsallts_open1(struct inode *inode, struct file *file)
{
	return single_open(file, tsallts_read1, NULL);
}

static const struct file_operations tsallts_fops1 = {
	.owner = THIS_MODULE,
	.open = tsallts_open1,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = tsallts_write1,
	.release = single_release,
};

static int tsallts_open2(struct inode *inode, struct file *file)
{
	return single_open(file, tsallts_read2, NULL);
}

static const struct file_operations tsallts_fops2 = {
	.owner = THIS_MODULE,
	.open = tsallts_open2,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = tsallts_write2,
	.release = single_release,
};


static int tsallts_open3(struct inode *inode, struct file *file)
{
	return single_open(file, tsallts_read3, NULL);
}

static const struct file_operations tsallts_fops3 = {
	.owner = THIS_MODULE,
	.open = tsallts_open3,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = tsallts_write3,
	.release = single_release,
};


static int tsallts_open4(struct inode *inode, struct file *file)
{
	return single_open(file, tsallts_read4, NULL);
}

static const struct file_operations tsallts_fops4 = {
	.owner = THIS_MODULE,
	.open = tsallts_open4,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = tsallts_write4,
	.release = single_release,
};


static int __init tsallts_init(void)
{
	int err = 0;
	struct proc_dir_entry *entry = NULL;
	struct proc_dir_entry *tsallts_dir = NULL;

    tsallts_dprintk("[tsallts_init] \n");

	err = tsallts_register_thermal();
    tsallts_dprintk("[tsallts_init] \n");

	if (err)
		goto err_unreg;

    tsallts_dprintk( "tsallts_register_thermal:err=%x \n",err);

    tsallts_dir = mtk_thermal_get_proc_drv_therm_dir_entry();
    if (!tsallts_dir)
    {
        tsallts_dprintk("[%s]: mkdir /proc/driver/thermal failed\n", __func__);
    }
    else
    {
        entry = proc_create("tzts1",  S_IRUGO | S_IWUSR | S_IWGRP, tsallts_dir, &tsallts_fops1);
        if (entry) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
            proc_set_user(entry, 0, 1000);
#else
            entry->gid = 1000;
#endif
		}

        entry = proc_create("tzts2",  S_IRUGO | S_IWUSR | S_IWGRP, tsallts_dir, &tsallts_fops2);
        if (entry) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
            proc_set_user(entry, 0, 1000);
#else
            entry->gid = 1000;
#endif
		}

        entry = proc_create("tzts3",  S_IRUGO | S_IWUSR | S_IWGRP, tsallts_dir, &tsallts_fops3);
        if (entry) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
            proc_set_user(entry, 0, 1000);
#else
            entry->gid = 1000;
#endif
		}
    }

	return 0;

	err_unreg:
	tsallts_unregister_cooler();
	return err;
}

static void __exit tsallts_exit(void)
{
	tsallts_dprintk("[tsallts_exit] \n");
	tsallts_unregister_thermal();
	tsallts_unregister_cooler();
}

module_init(tsallts_init);
module_exit(tsallts_exit);
