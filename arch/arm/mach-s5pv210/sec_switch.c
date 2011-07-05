/*
 * UART/USB path switching driver for Samsung Electronics devices.
 *
 *  Copyright (C) 2010 Samsung Electronics
 *  Ikkeun Kim <iks.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <mach/param.h>
#include <linux/fsa9480.h>
#include <linux/sec_battery.h>
#include <asm/mach/arch.h>
#include <linux/regulator/consumer.h>
#include <mach/gpio.h>
#include <mach/gpio-p1.h>
#include <mach/sec_switch.h>
#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <plat/devs.h>


#if defined(CONFIG_KOR_MODEL_M180K) || defined(CONFIG_KOR_MODEL_M180W)
#define _SUPPORT_WIMAX_UART_USB_PATH_
#endif

struct sec_switch_struct {
	struct sec_switch_platform_data *pdata;
	int switch_sel;
	int usb_path;
	int uart_owner;
};

struct sec_switch_wq {
	struct delayed_work work_q;
	struct sec_switch_struct *sdata;
	struct list_head entry;
};

#ifdef _FMC_DM_
extern struct class *sec_class;
#endif

#if defined(CONFIG_TARGET_LOCALE_KOR)
static int sec_switch_started;
#if defined(CONFIG_KEYBOARD_P1)
//I don't like this kind of coding style but I can't help but do like this at the moment.
extern bool keyboard_enable;
#endif
#endif

extern struct device *switch_dev;
static int switchsel;
// Get SWITCH_SEL param value from kernel CMDLINE parameter.
__module_param_call("", switchsel, param_set_int, param_get_int, &switchsel, 0, 0444);
MODULE_PARM_DESC(switchsel, "Switch select parameter value.");


static void usb_switch_mode(struct sec_switch_struct *secsw, int mode)
{
	if(mode == SWITCH_PDA)
	{
		if(secsw->pdata && secsw->pdata->set_regulator)
			secsw->pdata->set_regulator(AP_VBUS_ON);
		mdelay(10);

		fsa9480_manual_switching(AUTO_SWITCH);
	}
	else  // SWITCH_MODEM
	{
		if(secsw->pdata && secsw->pdata->set_regulator)
			secsw->pdata->set_regulator(CP_VBUS_ON);
		mdelay(10);

		fsa9480_manual_switching(SWITCH_V_Audio_Port);
	}
}

/* for sysfs control (/sys/class/sec/switch/usb_sel) */
static ssize_t usb_sel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);

#if defined(CONFIG_TARGET_LOCALE_KOR)
	switch(secsw->usb_path)
	{
		case 0:
			return sprintf(buf, "[USB Switch] Current USB owner = MODEM\n");
		case 1:
			return sprintf(buf, "[USB Switch] Current USB owner = PDA\n");
#ifdef _SUPPORT_WIMAX_UART_USB_PATH_
		case 2:
			return sprintf(buf, "[USB Switch] Current USB owner = WIMAX\n");
#endif
		default:
			return sprintf(buf, "usb_sel_show error\n");
	}
#else
	int usb_path = secsw->switch_sel & (int)(USB_SEL_MASK);

	return sprintf(buf, "USB Switch : %s\n", usb_path==SWITCH_PDA?"PDA":"MODEM");
#endif
}

static ssize_t usb_sel_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);

	printk("\n");

	if (sec_get_param_value)
		sec_get_param_value(__SWITCH_SEL, &(secsw->switch_sel));

	if(strncmp(buf, "PDA", 3) == 0 || strncmp(buf, "pda", 3) == 0) {
		usb_switch_mode(secsw, SWITCH_PDA);
//		usb_switching_value_update(SWITCH_PDA);
		secsw->usb_path = 1;
		secsw->switch_sel |= USB_SEL_MASK;
	}

	if(strncmp(buf, "MODEM", 5) == 0 || strncmp(buf, "modem", 5) == 0) {
		usb_switch_mode(secsw, SWITCH_MODEM);
//		usb_switching_value_update(SWITCH_MODEM);	
		secsw->usb_path = 0;
		secsw->switch_sel &= ~USB_SEL_MASK;
	}

#ifdef _SUPPORT_WIMAX_UART_USB_PATH_
	if (strncmp(buf, "WIMAX", 5) == 0 || strncmp(buf, "wimax", 5) == 0) {
		usb_switch_mode(secsw, SWITCH_MODEM);
		secsw->usb_path = 2;
	}
#endif

//	switching_value_update();

	if (sec_set_param_value)
		sec_set_param_value(__SWITCH_SEL, &(secsw->switch_sel));

	// update shared variable.
	if(secsw->pdata && secsw->pdata->set_switch_status)
		secsw->pdata->set_switch_status(secsw->switch_sel);

	return size;
}

static DEVICE_ATTR(usb_sel, 0664, usb_sel_show, usb_sel_store);


static ssize_t uart_switch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);

#if defined(CONFIG_TARGET_LOCALE_KOR)
	switch(secsw->uart_owner)
	{
		case 0:
			return sprintf(buf, "[UART Switch] Current UART owner = MODEM\n");
		case 1:
			return sprintf(buf, "[UART Switch] Current UART owner = PDA\n");
#ifdef _SUPPORT_WIMAX_UART_USB_PATH_
		case 2:
			return sprintf(buf, "[UART Switch] Current UART owner = WIMAX\n");
#endif
		default:
			return sprintf(buf, "uart_switch_show error\n");
	}
#else
	if (secsw->uart_owner)
		return sprintf(buf, "[UART Switch] Current UART owner = PDA \n");
	else			
		return sprintf(buf, "[UART Switch] Current UART owner = MODEM \n");
#endif
}

static ssize_t uart_switch_store(struct device *dev, struct device_attribute *attr,	const char *buf, size_t size)
{	
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	
	if (sec_get_param_value)
		sec_get_param_value(__SWITCH_SEL, &(secsw->switch_sel));

	if (strncmp(buf, "PDA", 3) == 0 || strncmp(buf, "pda", 3) == 0) {
#ifdef _SUPPORT_WIMAX_UART_USB_PATH_
		gpio_set_value(GPIO_UART_SEL1, 0);
#endif
		gpio_set_value(GPIO_UART_SEL, 1);
//		uart_switching_value_update(SWITCH_PDA);
		secsw->uart_owner = 1;
		secsw->switch_sel |= UART_SEL_MASK;
		printk("[UART Switch] Path : PDA\n");	
	}	

	if (strncmp(buf, "MODEM", 5) == 0 || strncmp(buf, "modem", 5) == 0) {
#if defined(CONFIG_TARGET_LOCALE_KOR) && defined(CONFIG_KEYBOARD_P1)
		if (!keyboard_enable)
#endif
		gpio_set_value(GPIO_UART_SEL, 0);
//		uart_switching_value_update(SWITCH_MODEM);
		secsw->uart_owner = 0;
		secsw->switch_sel &= ~UART_SEL_MASK;
		printk("[UART Switch] Path : MODEM\n");	
	}

#ifdef _SUPPORT_WIMAX_UART_USB_PATH_
	if (strncmp(buf, "PBA", 3) == 0 || strncmp(buf, "pba", 3) == 0) {
#if defined(CONFIG_TARGET_LOCALE_KOR) && defined(CONFIG_KEYBOARD_P1)
		if (!keyboard_enable)
#endif
		gpio_set_value(GPIO_UART_SEL, 0);
		secsw->uart_owner = 0;
		printk("[UART Switch] Path : PBA\n");
	}

	if (strncmp(buf, "WIMAX", 5) == 0 || strncmp(buf, "wimax", 5) == 0) {
#if defined(CONFIG_TARGET_LOCALE_KOR) && defined(CONFIG_KEYBOARD_P1)
		if (!keyboard_enable)
#endif
		{
			gpio_set_value(GPIO_UART_SEL1, 1);
			gpio_set_value(GPIO_UART_SEL, 1);
		}
		secsw->uart_owner = 2;
		printk("[UART Switch] Path : WIMAX\n");
	}
#endif

//	switching_value_update();

	if (sec_set_param_value)
		sec_set_param_value(__SWITCH_SEL, &(secsw->switch_sel));

	// update shared variable.
	if(secsw->pdata && secsw->pdata->set_switch_status)
		secsw->pdata->set_switch_status(secsw->switch_sel);

	return size;
}

static DEVICE_ATTR(uart_sel, 0664, uart_switch_show, uart_switch_store);


// for sysfs control (/sys/class/sec/switch/usb_state)
static ssize_t usb_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	int cable_state = CABLE_TYPE_NONE;

	if(secsw->pdata && secsw->pdata->get_cable_status)
		cable_state = secsw->pdata->get_cable_status();

	return sprintf(buf, "%s\n", (cable_state==CABLE_TYPE_USB)?"USB_STATE_CONFIGURED":"USB_STATE_NOTCONFIGURED");
} 

static ssize_t usb_state_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	printk("\n");
	return size;
}

static DEVICE_ATTR(usb_state, 0664, usb_state_show, usb_state_store);


// for sysfs control (/sys/class/sec/switch/disable_vbus)
static ssize_t disable_vbus_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("\n");
	return 0;
} 

static ssize_t disable_vbus_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	printk("%s\n", __func__);
	if(secsw->pdata && secsw->pdata->set_regulator)
		secsw->pdata->set_regulator(AP_VBUS_OFF);

	return size;
}

static DEVICE_ATTR(disable_vbus, 0664, disable_vbus_show, disable_vbus_store);


#ifdef _FMC_DM_
/* for sysfs control (/sys/class/sec/switch/.usb_lock/enable) */
static ssize_t enable_show
(
	struct device *dev,
	struct device_attribute *attr,
	char *buf
)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	int usb_access_lock;

	usb_access_lock = ((secsw->switch_sel & USB_LOCK_MASK) ? 1 : 0);

	if(usb_access_lock) {
		return snprintf(buf, PAGE_SIZE, "USB_LOCK");
	} else {
		return snprintf(buf, PAGE_SIZE, "USB_UNLOCK");
	}
}

static ssize_t enable_store
(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size
)
{
	struct usb_gadget *gadget = platform_get_drvdata(&s3c_device_usbgadget);
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	int value;
	int usb_access_lock;
	int cable_state = CABLE_TYPE_NONE;

	if (sscanf(buf, "%d", &value) != 1) {
		printk(KERN_ERR "enable_store: Invalid value\n");
		return -EINVAL;
	}

	if((value < 0) || (value > 1)) {
		printk(KERN_ERR "enable_store: Invalid value\n");
		return -EINVAL;
	}

	if (sec_get_param_value)
		sec_get_param_value(__SWITCH_SEL, &(secsw->switch_sel));

	usb_access_lock = ((secsw->switch_sel & USB_LOCK_MASK) ? 1 : 0);

	if(value != usb_access_lock) {
		if(secsw->pdata && secsw->pdata->get_cable_status)
			cable_state = secsw->pdata->get_cable_status();

		secsw->switch_sel &= ~USB_LOCK_MASK;

		if(value == 1) {
			secsw->switch_sel |= USB_LOCK_MASK;
			if(cable_state == CABLE_TYPE_USB)
				usb_gadget_vbus_disconnect(gadget);
		} else {
			if(cable_state == CABLE_TYPE_USB)
				usb_gadget_vbus_connect(gadget);
		}

		if (sec_set_param_value) {
			sec_set_param_value(__SWITCH_SEL, &(secsw->switch_sel));
		}

		// update shared variable.
		if(secsw->pdata && secsw->pdata->set_switch_status)
			secsw->pdata->set_switch_status(secsw->switch_sel);
	}

	return size;
}

static DEVICE_ATTR(enable, 0664, enable_show, enable_store);
#endif


static void sec_switch_init_work(struct work_struct *work)
{
	struct delayed_work *dw = container_of(work, struct delayed_work, work);
	struct sec_switch_wq *wq = container_of(dw, struct sec_switch_wq, work_q);
	struct sec_switch_struct *secsw = wq->sdata;
	int usb_sel = 0;
	int uart_sel = 0;
	int ret = 0;
#if defined(CONFIG_TARGET_LOCALE_KOR)
	struct usb_gadget *gadget = platform_get_drvdata(&s3c_device_usbgadget);
	int cable_state = CABLE_TYPE_NONE;
#endif
#ifdef _FMC_DM_
	int usb_access_lock;
#endif

//	printk("%s : called!!\n", __func__);

#if defined(CONFIG_TARGET_LOCALE_KOR)
	/* check param function pointer to keep a compatabilty with old bootloader */
	if (sec_get_param_value) {
		sec_get_param_value(__SWITCH_SEL, &switchsel);
		secsw->switch_sel = switchsel;
		cancel_delayed_work(&wq->work_q);
	} else {
		if(!sec_switch_started) {
			sec_switch_started = 1;
			schedule_delayed_work(&wq->work_q, msecs_to_jiffies(3000));
		} else {
			schedule_delayed_work(&wq->work_q, msecs_to_jiffies(100));
		}
		return;
	}
#else
	if (!regulator_get(NULL, "vbus_ap")  || !(secsw->pdata->get_phy_init_status())) {
		schedule_delayed_work(&wq->work_q, msecs_to_jiffies(100));
		return ;
	}
	else {
		cancel_delayed_work(&wq->work_q);
	}
#endif

	if(secsw->pdata && secsw->pdata->get_regulator) {
		ret = secsw->pdata->get_regulator();
		if(ret != 0) {
			pr_err("%s : failed to get regulators\n", __func__);
			return ;
		}
	}

	// init shared variable.
	if(secsw->pdata && secsw->pdata->set_switch_status)
		secsw->pdata->set_switch_status(secsw->switch_sel);

	usb_sel = secsw->switch_sel & (int)(USB_SEL_MASK);
	uart_sel = (secsw->switch_sel & (int)(UART_SEL_MASK)) >> 1;

	printk("%s : initial usb_sel(%d), uart_sel(%d)\n", __func__, usb_sel, uart_sel);

	// init UART/USB path.
	if(usb_sel) {
		usb_switch_mode(secsw, SWITCH_PDA);
		secsw->usb_path = 1;
	}
	else {
		usb_switch_mode(secsw, SWITCH_MODEM);
		secsw->usb_path = 0;
	}

#if defined(CONFIG_TARGET_LOCALE_KOR) && defined(CONFIG_KEYBOARD_P1)
	if (!keyboard_enable)
#endif
	{
	if(uart_sel) {
		gpio_set_value(GPIO_UART_SEL, 1);
		secsw->uart_owner = 1;
	}
	else {
		gpio_set_value(GPIO_UART_SEL, 0);
		secsw->uart_owner = 0;
	}
	}

#if defined(CONFIG_TARGET_LOCALE_KOR)
	if(secsw->pdata && secsw->pdata->get_cable_status)
		cable_state = secsw->pdata->get_cable_status();

#ifdef _FMC_DM_
	usb_access_lock = ((secsw->switch_sel & USB_LOCK_MASK) ? 1 : 0);

	if(!usb_access_lock)
#endif
	if(cable_state == CABLE_TYPE_USB) {
		(void)usb_gadget_vbus_connect(gadget);
	}
#endif
}

static int sec_switch_probe(struct platform_device *pdev)
{
	struct sec_switch_struct *secsw;
	struct sec_switch_platform_data *pdata = pdev->dev.platform_data;
	struct sec_switch_wq *wq;
#ifdef _FMC_DM_
	struct device *usb_lock;
#endif

	if (!pdata) {
		pr_err("%s : pdata is NULL.\n", __func__);
		return -ENODEV;
	}

	secsw = kzalloc(sizeof(struct sec_switch_struct), GFP_KERNEL);
	if (!secsw) {
		pr_err("%s : failed to allocate memory\n", __func__);
		return -ENOMEM;
	}

	printk("%s : *** switch_sel (0x%x)\n", __func__, switchsel);

	secsw->pdata = pdata;
	secsw->switch_sel = switchsel;

	dev_set_drvdata(switch_dev, secsw);

	// create sysfs files.
	if (device_create_file(switch_dev, &dev_attr_uart_sel) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_uart_sel.attr.name);

	if (device_create_file(switch_dev, &dev_attr_usb_sel) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_usb_sel.attr.name);

	if (device_create_file(switch_dev, &dev_attr_usb_state) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_usb_state.attr.name);

	if (device_create_file(switch_dev, &dev_attr_disable_vbus) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_disable_vbus.attr.name);

#ifdef _FMC_DM_
	usb_lock = device_create(sec_class, switch_dev, MKDEV(0, 0), NULL, ".usb_lock");
	if (IS_ERR(usb_lock)) {
		pr_err("Failed to create device(usb_lock)!\n");
	} else {
		dev_set_drvdata(usb_lock, secsw);

		if (device_create_file(usb_lock, &dev_attr_enable) < 0) {
			pr_err("Failed to create device file(%s)!\n", dev_attr_enable.attr.name);
			device_destroy((struct class *)usb_lock, MKDEV(0, 0));
		}
	}
#endif

	// run work queue
	wq = kmalloc(sizeof(struct sec_switch_wq), GFP_ATOMIC);
	if (wq) {
		wq->sdata = secsw;
#if defined(CONFIG_TARGET_LOCALE_KOR)
		sec_switch_started = 0;
#endif
		INIT_DELAYED_WORK(&wq->work_q, sec_switch_init_work);
		schedule_delayed_work(&wq->work_q, msecs_to_jiffies(100));
	}
	else
		return -ENOMEM;

	return 0;
}

static int sec_switch_remove(struct platform_device *pdev)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(&pdev->dev);
	
	kfree(secsw);

	return 0;
}

static struct platform_driver sec_switch_driver = {
	.probe = sec_switch_probe,
	.remove = sec_switch_remove,
	.driver = {
			.name = "sec_switch",
			.owner = THIS_MODULE,
	},
};

static int __init sec_switch_init(void)
{
	return platform_driver_register(&sec_switch_driver);
}

static void __exit sec_switch_exit(void)
{
	platform_driver_unregister(&sec_switch_driver);
}

module_init(sec_switch_init);
module_exit(sec_switch_exit);

MODULE_AUTHOR("Ikkeun Kim <iks.kim@samsung.com>");
MODULE_DESCRIPTION("Samsung Electronics Corp Switch driver");
MODULE_LICENSE("GPL");
