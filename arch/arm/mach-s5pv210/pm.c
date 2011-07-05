/* linux/arch/arm/mach-s5pv210/pm.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5PV210 - Power Management support
 *
 * Based on arch/arm/mach-s3c2410/pm.c
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/io.h>

#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/regs-timer.h>

#include <mach/regs-irq.h>
#include <mach/regs-clock.h>
#include <mach/regs-mem.h>
#include <mach/power-domain.h>

static struct sleep_save core_save[] = {
	/* PLL Control */
	SAVE_ITEM(S5P_APLL_CON),
	SAVE_ITEM(S5P_MPLL_CON),
	SAVE_ITEM(S5P_EPLL_CON),
	SAVE_ITEM(S5P_VPLL_CON),

	/* Clock source */
	SAVE_ITEM(S5P_CLK_SRC0),
	SAVE_ITEM(S5P_CLK_SRC1),
	SAVE_ITEM(S5P_CLK_SRC2),
	SAVE_ITEM(S5P_CLK_SRC3),
	SAVE_ITEM(S5P_CLK_SRC4),
	SAVE_ITEM(S5P_CLK_SRC5),
	SAVE_ITEM(S5P_CLK_SRC6),

	/* Clock source Mask */
	SAVE_ITEM(S5P_CLK_SRC_MASK0),
	SAVE_ITEM(S5P_CLK_SRC_MASK1),

	/* Clock Divider */
	SAVE_ITEM(S5P_CLK_DIV0),
	SAVE_ITEM(S5P_CLK_DIV1),
	SAVE_ITEM(S5P_CLK_DIV2),
	SAVE_ITEM(S5P_CLK_DIV3),
	SAVE_ITEM(S5P_CLK_DIV4),
	SAVE_ITEM(S5P_CLK_DIV5),
	SAVE_ITEM(S5P_CLK_DIV6),
	SAVE_ITEM(S5P_CLK_DIV7),

	/* Clock Main Gate */
	SAVE_ITEM(S5P_CLKGATE_MAIN0),
	SAVE_ITEM(S5P_CLKGATE_MAIN1),
	SAVE_ITEM(S5P_CLKGATE_MAIN2),

	/* Clock source Peri Gate */
	SAVE_ITEM(S5P_CLKGATE_PERI0),
	SAVE_ITEM(S5P_CLKGATE_PERI1),

	/* Clock source SCLK Gate */
	SAVE_ITEM(S5P_CLKGATE_SCLK0),
	SAVE_ITEM(S5P_CLKGATE_SCLK1),

	/* Clock IP Clock gate */
	SAVE_ITEM(S5P_CLKGATE_IP0),
	SAVE_ITEM(S5P_CLKGATE_IP1),
	SAVE_ITEM(S5P_CLKGATE_IP2),
	SAVE_ITEM(S5P_CLKGATE_IP3),
	SAVE_ITEM(S5P_CLKGATE_IP4),

	/* Clock Blcok and Bus gate */
	SAVE_ITEM(S5P_CLKGATE_BLOCK),
	SAVE_ITEM(S5P_CLKGATE_IP5),

	/* Clock ETC */
	SAVE_ITEM(S5P_CLK_OUT),
	SAVE_ITEM(S5P_MDNIE_SEL),

	/* PWM Register */
	SAVE_ITEM(S3C2410_TCFG0),
	SAVE_ITEM(S3C2410_TCFG1),
	SAVE_ITEM(S3C64XX_TINT_CSTAT),
	SAVE_ITEM(S3C2410_TCON),
	SAVE_ITEM(S3C2410_TCNTB(0)),
	SAVE_ITEM(S3C2410_TCMPB(0)),
	SAVE_ITEM(S3C2410_TCNTO(0)),
};

#if defined(CONFIG_KOR_MODEL_M180K) || defined(CONFIG_KOR_MODEL_M180W)
#ifndef FEATURE_FTM_SLEEP
#define FEATURE_FTM_SLEEP
#endif
#endif

#ifdef FEATURE_FTM_SLEEP
extern void (*pm_enable_rtctic)(unsigned int sec);
extern void (*pm_disable_rtctic)(void);

static void __iomem *rtc_pa_base;
#endif

#ifdef FEATURE_FTM_SLEEP
void s5pv210_pm_enable_rtctic(unsigned int sec) 
{
	unsigned int tmp;

	tmp = __raw_readl(rtc_pa_base+0x40); //RTCCON
	//printk(KERN_DEBUG "rtccon = 0x%x\n", tmp);

	//RTCEN
	tmp |= 0x1<<0;
	__raw_writel(tmp, rtc_pa_base+0x40);
	tmp = __raw_readl(rtc_pa_base+0x40); //RTCCON

	//Tick Time disable
	tmp &= ~(0x1<<8); //set RTCCON[0] as 1 for start, 
	__raw_writel(tmp, rtc_pa_base+0x40);
	tmp = __raw_readl(rtc_pa_base+0x40); //RTCCON
	//printk(KERN_DEBUG "rtccon set | 0x0<<8 = 0x%x\n", tmp);

	//TICCKSEL
	tmp &= ~(0xF<<4); // set TICCKSEL as 0000 for 32768Hz
	__raw_writel(tmp, rtc_pa_base+0x40);
	tmp = __raw_readl(rtc_pa_base+0x40); //RTCCON
	//printk(KERN_DEBUG "rtccon set | 0x0<<4 = 0x%x\n", tmp);

	//TICNT
	tmp = (sec * 32768) - 1 ; // set new time tick count value
	__raw_writel(tmp, rtc_pa_base+0x44);
	tmp = __raw_readl(rtc_pa_base+0x44); //TICNT
	//printk(KERN_DEBUG "ticnt set (%dsec * 32768) - 1 = 0x%x\n", sec, tmp);

	//TICEN
	tmp = __raw_readl(rtc_pa_base+0x40); //RTCCON
	tmp |= (0x1<<8); //Enables Tick Timer
	__raw_writel(tmp, rtc_pa_base+0x40);
	tmp = __raw_readl(rtc_pa_base+0x40);
	//printk(KERN_DEBUG "rtccon set | (0x1<<8), tick enable = 0x%x\n", tmp);

	//RTCEN clear
	tmp &= ~(0x1<<0);
	__raw_writel(tmp, rtc_pa_base+0x40);
	tmp = __raw_readl(rtc_pa_base+0x40); //RTCCON
	//printk(KERN_DEBUG "rtccon set & ~(0x1<<0), RTCEN clear = 0x%x\n", tmp);

	tmp = __raw_readl(rtc_pa_base+0x30); //INTP, intterrupt pending clear
	tmp |= 0x1<<0; // clear Time TIC bits
	__raw_writel(tmp, rtc_pa_base+0x30);
	tmp = __raw_readl(rtc_pa_base+0x30); //INTP, intterrupt pending clear
	//printk(KERN_DEBUG "intp time tic pending clear? = 0x%x\n", tmp);
}

void s5pv210_pm_disable_rtctic(void)
{
	unsigned int tmp;

	tmp = __raw_readl(rtc_pa_base+0x40);
	//printk(KERN_DEBUG "rtccon = 0x%x\n", tmp);

	//Tick Time disable
	tmp &= ~(0x1<<8); //set RTCCON[0] as 1 for start, 
	__raw_writel(tmp, rtc_pa_base+0x40);
	tmp = __raw_readl(rtc_pa_base+0x40); //RTCCON
	//printk(KERN_DEBUG "rtccon set | 0x0<<8 = 0x%x\n", tmp);

	tmp = __raw_readl(rtc_pa_base+0x30);
	//printk(KERN_DEBUG "intp = 0x%x\n", tmp);

	tmp |= 0x1<<0;
	__raw_writel(tmp, rtc_pa_base+0x30);
	tmp = __raw_readl(rtc_pa_base+0x30);
	//printk(KERN_DEBUG "intp time tic pending clear? = 0x%x\n", tmp);
}
#endif

void s5pv210_cpu_suspend(void)
{
	unsigned long tmp;

	/* issue the standby signal into the pm unit. Note, we
	 * issue a write-buffer drain just in case */

	tmp = 0;

	asm("b 1f\n\t"
	    ".align 5\n\t"
	    "1:\n\t"
	    "mcr p15, 0, %0, c7, c10, 5\n\t"
	    "mcr p15, 0, %0, c7, c10, 4\n\t"
	    "wfi" : : "r" (tmp));

	/* we should never get past here */
	panic("sleep resumed to originator?");
}

static void s5pv210_pm_prepare(void)
{
	unsigned int tmp;

	/* ensure at least INFORM0 has the resume address */
	__raw_writel(virt_to_phys(s3c_cpu_resume), S5P_INFORM0);

	/* WFI for SLEEP mode configuration by SYSCON */
	tmp = __raw_readl(S5P_PWR_CFG);
	tmp &= S5P_CFG_WFI_CLEAN;
	tmp |= S5P_CFG_WFI_SLEEP;
	__raw_writel(tmp, S5P_PWR_CFG);

	/* SYSCON interrupt handling disable */
	tmp = __raw_readl(S5P_OTHERS);
	tmp |= S5P_OTHER_SYSC_INTOFF;
	__raw_writel(tmp, S5P_OTHERS);

	__raw_writel(0xffffffff, (VA_VIC0 + VIC_INT_ENABLE_CLEAR));
	__raw_writel(0xffffffff, (VA_VIC1 + VIC_INT_ENABLE_CLEAR));
	__raw_writel(0xffffffff, (VA_VIC2 + VIC_INT_ENABLE_CLEAR));
	__raw_writel(0xffffffff, (VA_VIC3 + VIC_INT_ENABLE_CLEAR));

	s3c_pm_do_save(core_save, ARRAY_SIZE(core_save));
}

static void s5pv210_pm_resume(void)
{
	u32 tmp;

	tmp = __raw_readl(S5P_OTHERS);
	tmp |= (S5P_OTHERS_RET_IO | S5P_OTHERS_RET_CF |\
		S5P_OTHERS_RET_MMC | S5P_OTHERS_RET_UART);
	__raw_writel(tmp , S5P_OTHERS);

	s3c_pm_do_restore_core(core_save, ARRAY_SIZE(core_save));
}

static __init int s5pv210_pm_drvinit(void)
{
	pm_cpu_prep = s5pv210_pm_prepare;
	pm_cpu_sleep = s5pv210_cpu_suspend;
	pm_cpu_restore = s5pv210_pm_resume;

#ifdef FEATURE_FTM_SLEEP
	pm_enable_rtctic = s5pv210_pm_enable_rtctic;
	pm_disable_rtctic = s5pv210_pm_disable_rtctic;
#endif	

#ifdef FEATURE_FTM_SLEEP
    rtc_pa_base = ioremap(S5PV210_PA_RTC, 0x100); //0xe2800000 //S5PV210_PA_RTC
    if(!rtc_pa_base) {
            printk(KERN_DEBUG "Unable to allocate rtc memory\n");
            return -1;
    }
#endif

	return 0;
}

arch_initcall(s5pv210_pm_drvinit);
