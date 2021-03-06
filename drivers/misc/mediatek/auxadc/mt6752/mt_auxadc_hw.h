#ifndef _MTK_ADC_HW_H
#define _MTK_ADC_HW_H
 
#ifdef CONFIG_OF
extern void __iomem *auxadc_base;
extern void __iomem *auxadc_apmix_base;
#undef AUXADC_BASE
#undef APMIXED_BASE
#define AUXADC_BASE auxadc_base
#define APMIXED_BASE auxadc_apmix_base
#else
#include <mach/mt_reg_base.h>
#endif

#define ADC_CHANNEL_MAX 16

#define MT_PDN_PERI_AUXADC MT_CG_INFRA_AUXADC

#define AUXADC_CON0             (AUXADC_BASE + 0x000)
#define AUXADC_CON1             (AUXADC_BASE + 0x004)
#define AUXADC_CON2             (AUXADC_BASE + 0x010)
#define AUXADC_DAT0             (AUXADC_BASE + 0x014)
#define AUXADC_TP_CMD           (AUXADC_BASE + 0x005c)
#define AUXADC_TP_ADDR          (AUXADC_BASE + 0x0060)
#define AUXADC_TP_CON0          (AUXADC_BASE + 0x0064)
#define AUXADC_TP_DATA0         (AUXADC_BASE + 0x0074)
#define AUXADC_DET_VOLT         (AUXADC_BASE + 0x084)
#define AUXADC_DET_SEL          (AUXADC_BASE + 0x088)
#define AUXADC_DET_PERIOD       (AUXADC_BASE + 0x08C)
#define AUXADC_DET_DEBT         (AUXADC_BASE + 0x090)
#define AUXADC_SPL_EN           (AUXADC_BASE + 0x0B0)

#define AUXADC_GE_A             (0xF0206548)
#define AUXADC_GE_A_MASK        (0x3FF)
#define AUXADC_GE_A_SHIFT       (0)
#define AUXADC_OE_A             (0xF0206548)
#define AUXADC_OE_A_MASK        (0x3FF)
#define AUXADC_OE_A_SHIFT       (10)
#define AUXADC_CALI_EN_A        (0xF0206548)
#define AUXADC_CALI_EN_A_MASK   (0x1)
#define AUXADC_CALI_EN_A_SHIFT  (20)

#define PAD_AUX_XP				13
#define PAD_AUX_YM				15
#define TP_CMD_ADDR_X			0x0005

#define AUXADC_CON_RTP			(APMIXED_BASE + 0x0404)

#endif   /*_MTK_ADC_HW_H*/

