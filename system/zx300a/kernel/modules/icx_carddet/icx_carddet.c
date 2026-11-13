/*
 * Copyright 2012, 2016, 2017, 2018 Sony Corporation
 */
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <mach/icx_carddet.h>
#include <mach/mt_gpio.h>
#include <mach/board.h>
#include <mach/eint.h>
#include <cust_eint.h>

#include "mt_sd.h"
#include "drivers/mmc/card/queue.h"

/****************/
/*@ Definitions */
/****************/
#define ENABLE_STAT_QUEUE
#define MODULE_NAME         "icx_carddet"

#ifdef BENTEN_M1_SD_POWER_SWITCH_GPIO
/* Not supported (Only use for debug) */
/* #define POWER_CONTROL_ENABLE_BY_INTERNAL */
#define POWER_CONTROL_ENABLE_BY_EXTERNAL
#endif /* BENTEN_M1_SD_POWER_SWITCH_GPIO */

/* for debug */
#define CARDDET_DEBUG   0

#define ERR_MSG(format, arg...)  pr_err("%s: ** Error ** " format "\n", \
					 MODULE_NAME, ## arg)
#define INFO_MSG(format, arg...) pr_info("%s: " format "\n", \
					 MODULE_NAME, ## arg)

#if CARDDET_DEBUG
#define DBG_MSG(format, arg...)  pr_warn("%s: " format "\n", \
					 MODULE_NAME, ## arg)
#else  /* CARDDET_DEBUG */
#define DBG_MSG(format, arg...)  do {} while (0)
#endif /* CARDDET_DEBUG */


/* Major & Minor */
#define CARDDET_DRV_MAJOR            0        /* 0:dynamic */
#define CARDDET_DRV_MINOR            0
#define CARDDET_CDEV_FILE_NAME       "icx_carddet"

/* PIN assign */
#define CARD_SD_DET                 1
#define CARD_MS_DET                 1
#define CARD_PWRCTL                 1
#define CHG_PINSEL_SD_SD            0x00000000
#define CHG_PINSEL_SD_MS            0x00000001

/* Request ID to thread */
enum {
	CARDDET_REQ_NONE = 0,
	CARDDET_REQ_IRQ,
	CARDDET_REQ_INIT,
};

/* Sequence of thread */
enum {
	CARDDET_SEQ_NONE = 0,
	CARDDET_SEQ_PWRON,
	CARDDET_SEQ_SET_PULL,
	CARDDET_SEQ_INIT,
	CARDDET_SEQ_RETRY,
};

/* Timeout */
#define CARDDET_TIMEOUT_IDLE                   0    /* none   */
#define CARDDET_TIMEOUT_WAIT_PWR_STABLE        100  /*   1  s */
#define CARDDET_TIMEOUT_WAIT_PULL_STABLE       1    /*  10 ms */
#define CARDDET_TIMEOUT_WAIT_INIT              90   /* 900 ms */
#define CARDDET_TIMEOUT_RETRY                  75   /* 750 ms */

/* Retry Count */
#define    CARDDET_RETRY_OVER           3

/* Sub Status */
#define CARDDET_STATE_FLAG_NONE         0x00
#define CARDDET_STATE_FLAG_WAIT_STABLE  0x01
#define CARDDET_STATE_FLAG_WAIT_REQUEST 0x02
#define CARDDET_STATE_FLAG_ALL          0x03

#ifndef MTK_DUALCARD_SUPPORT
#define CARDDET_CARD_MAX_NUM   0x01
#else
#define CARDDET_CARD_MAX_NUM   0x02
#endif /*MTK_DUALCARD_SUPPORT*/
/***************/
/*@ Structures */
/***************/
/* CARDDET device */
struct carddet_card_info {
	spinlock_t              lock;
	int                     irq;
	struct task_struct      *thread_task;
	wait_queue_head_t       thread_wq;
	int                     thread_req;
	int                     thread_seq;
	int                     thread_timeout;
	int                     det_state;
	int                     card_type;
	int                     read_only;
	int                     retry_count;
	unsigned char           insert_state_flag;
	struct msdc_host        *host_sd;
	int                     prev_state;
};

struct carddet_dev {
	int                     users;
	struct cdev             cdev;
	wait_queue_head_t       wqh;
	int                     suspend_flag;
#ifdef ENABLE_STAT_QUEUE
	struct list_head     list;
#else  /* ENABLE_STAT_QUEUE */
	int                     wake_up_flg;
#endif /* ENABLE_STAT_QUEUE */
	struct msdc_host        *host_sd;
	struct carddet_card_info card_info[2];
};

#ifdef ENABLE_STAT_QUEUE
struct carddet_detect_status {
	struct list_head entry;
	int              state;
	int              type;
	int              ro;
	int              state1;
	int              type1;
	int              ro1;
};
#endif /* ENABLE_STAT_QUEUE */

struct mmc_blk_data {
	spinlock_t lock;
	struct gendisk *disk;
	struct mmc_queue queue;
	unsigned int usage;
	unsigned int read_only;
};

static struct class *carddet_class;

/********************/
/*@ Local variables */
/********************/
static DEFINE_SEMAPHORE(carddet_sem);

/* Major & Minor */
static int carddet_major = CARDDET_DRV_MAJOR;
static int carddet_minor = CARDDET_DRV_MINOR;


module_param(carddet_major, int, S_IRUGO);
module_param(carddet_minor, int, S_IRUGO);


static int enable_req_det_func;

#if CARDDET_DEBUG
module_param(enable_req_det_func, int, S_IRUGO | S_IWUSR);
#endif


/* CARDDET devices */
static struct carddet_dev *carddet_devices;

/***************/
/*@ Prototypes */
/***************/
/* driver entry routines */
static int carddet_eint_handler(int irq, void *arg);
static int carddet_eint_handler1(int irq, void *arg);
static void carddet_detect_card_removed(int msdc_id);
static int  carddet_get_card_state(void);

/* user entry routines */
static int carddet_open(struct inode *inode, struct file *filp);
static int carddet_release(struct inode *inode, struct file *filp);
static long carddet_ioctl(struct file *filp, unsigned int cmd,
			unsigned long arg);
static unsigned int carddet_poll(struct file *file, poll_table *wait);

/* file operations */
static const struct file_operations carddet_fops = {
	.owner   = THIS_MODULE,
	.open    = carddet_open,
	.release = carddet_release,
	.unlocked_ioctl   = carddet_ioctl,
	.poll    = carddet_poll,
};

/* Local routines */
static int  carddet_notify_status(struct carddet_dev *dev);
static void carddet_control_power(struct carddet_card_info *card_info);
static int  carddet_check_result(struct carddet_card_info *card_info);
static int  carddet_ioc_get_info(struct carddet_dev *dev, unsigned long arg);
static int  carddet_ioc_control_power(struct carddet_dev *dev,
				      unsigned long arg);
static int  carddet_ioc_get_xdet(struct carddet_dev *dev, unsigned long arg);
static int  carddet_ioc_req_detect(struct carddet_dev *dev);

static struct carddet_eint_ops carddet_ops = {
	.notifier_call = carddet_eint_handler,
	.notifier_call1 = carddet_eint_handler1,
};

static struct carddet_func_tbl carddet_func = {
	.card_removed = carddet_detect_card_removed,
};

static struct carddet_to_msc_func carddet_to_msc_func_tbl = {
	.get_status = carddet_get_card_state,
};


/*******************/
/*@ LOCAL ROUTINES */
/*******************/
/* Get card status(for msc module) */
static int carddet_get_card_state(void)
{
	int ret = ICX_CARDDET_STATE_INSERTED;

	DBG_MSG("** %s called", __func__);

	if (carddet_devices == NULL)
		return ret;

	if (ICX_CARDDET_STATE_REMOVED ==
	    carddet_devices->card_info[0].det_state &&
	    ICX_CARDDET_STATE_REMOVED ==
	    carddet_devices->card_info[1].det_state)
		ret = ICX_CARDDET_STATE_REMOVED;

	return ret;
}

/* Notify the card status to user */
static int carddet_notify_status(struct carddet_dev *dev)
{
	int ret = 0;
#ifdef ENABLE_STAT_QUEUE
	struct carddet_detect_status *status;
#endif

	DBG_MSG("** %s called", __func__);

#ifdef ENABLE_STAT_QUEUE
	status = kmalloc(sizeof(struct carddet_detect_status), GFP_KERNEL);
	if (status) {
		status->state  = dev->card_info[0].det_state;
		status->type   = dev->card_info[0].card_type;
		status->ro     = dev->card_info[0].read_only;

		status->state1 = dev->card_info[1].det_state;
		status->type1  = dev->card_info[1].card_type;
		status->ro1    = dev->card_info[1].read_only;
		list_add_tail(&status->entry, &dev->list);

		wake_up_interruptible(&dev->wqh);
	} else {
		ret = -ENOMEM;
	}
#else  /* ENABLE_STAT_QUEUE */
	if (dev->wake_up_flg == 0) {
		dev->wake_up_flg = 1;

		wake_up_interruptible(&dev->wqh);
	}
#endif /* ENABLE_STAT_QUEUE */

	return ret;
}

/* Power control */
static void carddet_control_power(struct carddet_card_info *card_info)
{
#ifdef POWER_CONTROL_ENABLE_BY_INTERNAL
	int ret      = 0;
	int gpio     = 0;
	int gpio_num = 0;

	if (card_info->det_state == ICX_CARDDET_STATE_INSERTED)
		gpio = GPIO_OUT_ONE;
	else if (card_info->det_state == ICX_CARDDET_STATE_REMOVED)
		gpio = GPIO_OUT_ZERO;
	else
		gpio = GPIO_OUT_ZERO;

	if (card_info->host_sd != NULL) {
		gpio_num = card_info->host_sd->hw->pow_en;
		ret = mt_set_gpio_out(gpio_num, gpio);
	} else {
		ERR_MSG("carddet: msdc host is NULL sts(%d)",
				card_info->det_state);
	}

	DBG_MSG("** %s called ret(%d) gpio[%d](%d) sts(%d)",
		 __func__, ret, gpio_num, gpio, card_info->det_state);

#else  /* POWER_CONTROL_ENABLE_BY_INTERNAL */
	/* No operation for now */
	DBG_MSG("** %s called sts(%d) !!! NOP !!!",
		__func__, card_info->det_state);
#endif /* POWER_CONTROL_ENABLE_BY_INTERNAL */
}

static int carddet_get_hw_detect_state(struct carddet_card_info *card_info)
{
	int detect_status = 0;
	unsigned int polarity = 0;
	struct msdc_host *host = NULL;

	host = card_info->host_sd;
	if (host ==  NULL) {
		DBG_MSG("** %s called", __func__);
		return 0;
	}

#ifdef BENTEN_M1_SD_POWER_SWITCH_GPIO
	polarity = mt_eint_get_polarity(host->hw->eint_xins);
#else
	if (host->id == 1) {
#ifdef CUST_EINT_MSDC1_INS_NUM
		polarity = mt_eint_get_polarity(CUST_EINT_MSDC1_INS_NUM);
#endif/*CUST_EINT_MSDC1_INS_NUM*/
	} else {
#ifdef CUST_EINT_MSDC2_INS_NUM
		polarity = mt_eint_get_polarity(CUST_EINT_MSDC2_INS_NUM);
#endif/*CUST_EINT_MSDC2_INS_NUM*/
	}
#endif

	if (polarity ^ host->hw->cd_level)
		detect_status = 1;

	DBG_MSG("** %s called card(%d) detect_status(%d)",
		__func__, host->id, detect_status);

	return detect_status;
}

/* get driver data */
static int carddet_check_result(struct carddet_card_info *card_info)
{
	int           result;
	unsigned long flags;
	int ro  = 0;
	int ret = 0;
	struct msdc_host *host_sd;
	struct mmc_blk_data *md = NULL;

	DBG_MSG("** %s called", __func__);
	host_sd = card_info->host_sd;

	if (host_sd->mmc == NULL || host_sd->mmc->card == NULL) {
		DBG_MSG("** %s mmc or card == NULL ", __func__);
		result = ICX_CARDDET_RESULT_NG;
	} else {
		md = mmc_get_drvdata(host_sd->mmc->card);
		if (md != NULL) {
			result = ICX_CARDDET_RESULT_OK;
			ro = (host_sd->mmc->card->state & MMC_STATE_READONLY);
		} else {
			result = ICX_CARDDET_RESULT_NG;
		}
	}

	if (result == ICX_CARDDET_RESULT_OK) {
		DBG_MSG("Detection completed : type=%d ro=%d",
			 card_info->card_type, ro);

		spin_lock_irqsave(&card_info->lock, flags);
		card_info->det_state = ICX_CARDDET_STATE_DETECTED;
		card_info->read_only = ro;
		spin_unlock_irqrestore(&card_info->lock, flags);

		carddet_notify_status(carddet_devices);
	} else {
		if (card_info->retry_count >= CARDDET_RETRY_OVER) {
			DBG_MSG("Detection failed : type=%d",
				 card_info->card_type);

			spin_lock_irqsave(&card_info->lock, flags);
			card_info->det_state = ICX_CARDDET_STATE_NG;
			card_info->read_only = 0;
			spin_unlock_irqrestore(&card_info->lock, flags);

			carddet_control_power(card_info);

			carddet_notify_status(carddet_devices);
		} else {
			DBG_MSG("Detection retry(%d) : type=%d",
				 card_info->retry_count,
				 card_info->card_type);

			card_info->retry_count++;
			ret = -1;
		}
	}

	return ret;
}

/* Removed function */
static void carddet_card_removed(struct carddet_card_info *card_info)
{
	unsigned long flags;

	DBG_MSG("** %s called", __func__);

	card_info->thread_seq     = CARDDET_SEQ_NONE;
	card_info->thread_timeout = CARDDET_TIMEOUT_IDLE;

	spin_lock_irqsave(&card_info->lock, flags);
	card_info->det_state = ICX_CARDDET_STATE_REMOVED;
	card_info->card_type = ICX_CARDDET_TYPE_NONE;
	card_info->read_only = 0;
	card_info->insert_state_flag = CARDDET_STATE_FLAG_NONE;
	spin_unlock_irqrestore(&card_info->lock, flags);

	carddet_control_power(card_info);

	if (carddet_devices->suspend_flag == 0)
		carddet_notify_status(carddet_devices);

	card_info->thread_req = CARDDET_REQ_NONE;
}

static void carddet_detect_card_removed(int msdc_id)
{
	DBG_MSG("** %s called from msdc driver", __func__);

	if (msdc_id == 1)
		carddet_card_removed(&carddet_devices->card_info[0]);
	else
		carddet_card_removed(&carddet_devices->card_info[1]);
}

/* ioctl(CARD_DET_GET_INFO) */
static int carddet_ioc_get_info(struct carddet_dev *dev, unsigned long arg)
{
	icx_card_info_t info;
	unsigned long   flags;

	DBG_MSG("** %s called", __func__);

	memset(&info, 0, sizeof(icx_card_info_t));


#ifdef ENABLE_STAT_QUEUE
	if (!list_empty(&dev->list)) {
		/* pop status */
		struct carddet_detect_status *status =
			list_first_entry(&dev->list,
					 struct carddet_detect_status, entry);
		if (status) {
			spin_lock_irqsave(
				&carddet_devices->card_info[0].lock, flags);
			info.state = status->state;
			info.type  = status->type;
			info.ro    = status->ro;
			spin_unlock_irqrestore(
				&carddet_devices->card_info[0].lock, flags);

			spin_lock_irqsave(
				&carddet_devices->card_info[1].lock, flags);
			info.state1 = status->state1;
			info.type1  = status->type1;
			info.ro1    = status->ro1;
			spin_unlock_irqrestore(
				&carddet_devices->card_info[1].lock, flags);

			list_del(&status->entry);
			kfree(status);
		} else {
			spin_lock_irqsave(
				&carddet_devices->card_info[0].lock, flags);
			info.state  = dev->card_info[0].det_state;
			info.type   = dev->card_info[0].card_type;
			info.ro     = dev->card_info[0].read_only;
			spin_unlock_irqrestore(
				&carddet_devices->card_info[0].lock, flags);

			spin_lock_irqsave(
				&carddet_devices->card_info[1].lock, flags);
			info.state1 = dev->card_info[1].det_state;
			info.type1  = dev->card_info[1].card_type;
			info.ro1    = dev->card_info[1].read_only;
			spin_unlock_irqrestore(
				&carddet_devices->card_info[1].lock, flags);
		}
	} else {
		spin_lock_irqsave(
			&carddet_devices->card_info[0].lock, flags);
		info.state  = dev->card_info[0].det_state;
		info.type   = dev->card_info[0].card_type;
		info.ro     = dev->card_info[0].read_only;
		spin_unlock_irqrestore(
			&carddet_devices->card_info[0].lock, flags);

		spin_lock_irqsave(
			&carddet_devices->card_info[1].lock, flags);
		info.state1 = dev->card_info[1].det_state;
		info.type1  = dev->card_info[1].card_type;
		info.ro1    = dev->card_info[1].read_only;
		spin_unlock_irqrestore(
			&carddet_devices->card_info[1].lock, flags);
	}
#else  /* ENABLE_STAT_QUEUE */
	spin_lock_irqsave(&carddet_devices->card_info[0].lock, flags);
	info.state  = dev->card_info[0].det_state;
	info.type   = dev->card_info[0].card_type;
	info.ro     = dev->card_info[0].read_only;
	spin_unlock_irqrestore(&carddet_devices->card_info[0].lock, flags);

	spin_lock_irqsave(&carddet_devices->card_info[1].lock, flags);
	info.state1 = dev->card_info[1].det_state;
	info.type1  = dev->card_info[1].card_type;
	info.ro1    = dev->card_info[1].read_only;
	spin_unlock_irqrestore(&carddet_devices->card_info[1].lock, flags);
#endif /* ENABLE_STAT_QUEUE */

	if (copy_to_user((icx_card_info_t *)arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

/* ioctl(CARD_DET_CONTROL_POWER) */
static int carddet_ioc_control_power(struct carddet_dev *dev, unsigned long arg)
{
	int ret  = 0;
	int gpio = 0;
	int gpio_num = 0;
	unsigned int power_mode;
	struct carddet_card_info *card_info;

	DBG_MSG("** %s called", __func__);

	ret = copy_from_user(&power_mode,
			    (unsigned int *)arg, sizeof(power_mode));
	if (ret != 0) {
		ERR_MSG("carddet: %s copy_from_user failed", __func__);
		return -EFAULT;
	}
#ifdef POWER_CONTROL_ENABLE_BY_EXTERNAL
	if (power_mode & 0x10)
		card_info = &carddet_devices->card_info[1];
	else
		card_info = &carddet_devices->card_info[0];

	if (power_mode & 0xF) {
		/* power on */
		gpio = GPIO_OUT_ONE;
	} else {
		/* power off */
		gpio = GPIO_OUT_ZERO;
	}

	if (card_info->host_sd != NULL) {
		gpio_num = card_info->host_sd->hw->pow_en;
		ret = mt_set_gpio_out(gpio_num, gpio);
	} else {
		ERR_MSG("carddet: msdc host is NULL sts(%d)",
				card_info->det_state);
	}

	DBG_MSG("** %s called ret(%d) gpio[%d](%d) PM(%d)",
		__func__, ret, gpio_num, gpio, power_mode);

#else  /* POWER_CONTROL_ENABLE_BY_EXTERNAL */
	/* No operation for now */
	DBG_MSG("** %s called power_mode(%d) !!! NOP !!!",
		__func__, power_mode);
#endif /* POWER_CONTROL_ENABLE_BY_EXTERNAL */

	return 0;
}

/* ioctl(CARD_DET_GET_XDET) */
static int carddet_ioc_get_xdet(struct carddet_dev *dev, unsigned long arg)
{
	int detect_status = 0;
	int ret;
	struct carddet_card_info *card_info;

	DBG_MSG("** %s called", __func__);

	ret = copy_from_user(&detect_status,
			    (unsigned int *)arg, sizeof(detect_status));
	if (ret != 0) {
		ERR_MSG("carddet: %s copy_from_user failed", __func__);
		return -EFAULT;
	}

	if (detect_status & 0x10)
		card_info = &carddet_devices->card_info[1];
	else
		card_info = &carddet_devices->card_info[0];

	detect_status = carddet_get_hw_detect_state(card_info);

	if (copy_to_user((int *)arg, &detect_status, sizeof(int)))
		return -EFAULT;

	return ret;
}

/* ioctl(CARD_DET_REQ_DETECT) */
static int carddet_ioc_req_detect(struct carddet_dev *dev)
{
	unsigned long flags;
	int ret = 0;

	if (enable_req_det_func) {
		DBG_MSG("** %s called", __func__);

		spin_lock_irqsave(&carddet_devices->card_info[0].lock, flags);

		if (carddet_devices->card_info[0].insert_state_flag &
				CARDDET_STATE_FLAG_WAIT_REQUEST) {
			carddet_devices->card_info[0].insert_state_flag &=
				~CARDDET_STATE_FLAG_WAIT_REQUEST;

			if (!(carddet_devices->card_info[0].insert_state_flag &
					CARDDET_STATE_FLAG_WAIT_STABLE)) {
				carddet_devices->card_info[0].thread_req =
						CARDDET_REQ_INIT;
				wake_up_interruptible(
				&carddet_devices->card_info[0].thread_wq);
			}
		} else {
			ret = -EAGAIN;
		}

		spin_unlock_irqrestore(
			&carddet_devices->card_info[0].lock, flags);
	} else {
		DBG_MSG("** %s called", __func__);
	}

	return ret;
}

/************************/
/*@ USER ENTRY ROUTINES */
/************************/
/* OPEN */
static int carddet_open(struct inode *inode, struct file *filp)
{
	struct carddet_dev *dev;

	DBG_MSG("** %s called  :  open", __func__);

	dev = container_of(inode->i_cdev, struct carddet_dev, cdev);
	filp->private_data = dev;

	if (down_interruptible(&carddet_sem))
		return -ERESTARTSYS;

	dev->users++;
	DBG_MSG("** carddet: users=%d", dev->users);

	up(&carddet_sem);

	return 0;
}

/* CLOSE */
static int carddet_release(struct inode *inode, struct file *filp)
{
	struct carddet_dev *dev;

	DBG_MSG("** %s called  :  close", __func__);

	dev = filp->private_data;

	if (down_interruptible(&carddet_sem))
		return -ERESTARTSYS;

	dev->users--;
	DBG_MSG("   carddet: users=%d", dev->users);

	up(&carddet_sem);

	return 0;
}

/* IOCTL */
static long carddet_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct carddet_dev *dev = (struct carddet_dev *)filp->private_data;
	int ret = 0;

	DBG_MSG("** %s called cmd(0x%08x) except(0x%08x)", __func__,
		cmd, CARD_DET_GET_INFO);

	if (_IOC_TYPE(cmd) != CARD_DET_IOC_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) > CARD_DET_IOC_MAXNR)
		return -ENOTTY;

	switch (cmd) {
	case CARD_DET_GET_INFO:
		/* get card information */
		ret = carddet_ioc_get_info(dev, arg);
		break;

	case CARD_DET_CONTROL_POWER:
		/* control power */
		ret = carddet_ioc_control_power(dev, arg);
		break;

	case CARD_DET_GET_XDET:
		/* get card xdet status */
		ret = carddet_ioc_get_xdet(dev, arg);
		break;

	case CARD_DET_REQ_DETECT:
		/* request to detect a card */
		ret = carddet_ioc_req_detect(dev);
		break;

	default:
		ERR_MSG("** carddet_ioctl in OTHER:=0x%08x", cmd);
		ret = -ENOTTY;
		break;
	}

	return ret;
}

/* POLL */
static unsigned int carddet_poll(struct file *file, poll_table *wait)
{
	struct carddet_dev *dev = (struct carddet_dev *)file->private_data;
	int ret = 0;

	DBG_MSG("** %s called file_p=%08x wait_p=%08x", __func__,
		(unsigned int)file, (unsigned int)wait);

	poll_wait(file, &dev->wqh, wait);

#ifdef ENABLE_STAT_QUEUE
	if (!list_empty(&dev->list)) {
		struct carddet_detect_status *status =
				list_first_entry(&dev->list,
				struct carddet_detect_status, entry);
		if (status)
			INFO_MSG("%s status:(%d)(%d),type:(%d)(%d),ro:(%d)(%d)"
				, __func__, status->state, status->state1,
				status->type, status->type1,
				status->ro, status->ro1);

		ret |= POLLIN | POLLRDNORM;
	}
#else  /* ENABLE_STAT_QUEUE */
	if (dev->wake_up_flg != 0) {
		dev->wake_up_flg = 0;
		ret |= POLLIN | POLLRDNORM;
	}
#endif /* ENABLE_STAT_QUEUE */

	DBG_MSG("** carddet_poll: return(%d)", ret);

	return ret;
}

/**********************/
/*@ INTERRUPT HANDLER */
/**********************/
static irqreturn_t carddet_eint_detect(struct carddet_card_info *card_info)
{
	int got_bad_card = 0;
	unsigned long flags;
	struct msdc_host *host_sd = card_info->host_sd;

	DBG_MSG("** %s called", __func__);

	spin_lock_irqsave(&card_info->lock, flags);

	if (carddet_devices->suspend_flag == 0) {
		if (host_sd->hw->cd_level ^ host_sd->sd_cd_polarity) {
			got_bad_card = host_sd->block_bad_card;
			host_sd->card_inserted = 0;
			if (host_sd->mmc && host_sd->mmc->card) {
				DBG_MSG("** %s mmc_card_set_removed --called",
					 __func__);
				mmc_card_set_removed(host_sd->mmc->card);
			}
		}
	}

	host_sd->sd_cd_polarity = (~(host_sd->sd_cd_polarity)) & 0x1;
	host_sd->block_bad_card = 0;

	spin_unlock_irqrestore(&card_info->lock, flags);

#ifdef BENTEN_M1_SD_POWER_SWITCH_GPIO
	/* control Eint irq */
	if (0 == host_sd->sd_cd_polarity)
		mt_eint_set_polarity(host_sd->hw->eint_xins, MT_POLARITY_LOW);
	else
		mt_eint_set_polarity(host_sd->hw->eint_xins, MT_POLARITY_HIGH);
#else
	if (host_sd->id == 1) {
#ifdef CUST_EINT_MSDC1_INS_NUM
		/* control Eint irq */
		if (0 == host_sd->sd_cd_polarity)
			mt_eint_set_polarity(CUST_EINT_MSDC1_INS_NUM,
					MT_POLARITY_LOW);
		else
			mt_eint_set_polarity(CUST_EINT_MSDC1_INS_NUM,
					MT_POLARITY_HIGH);
#endif/*CUST_EINT_MSDC1_INS_NUM*/
	} else {
#ifdef CUST_EINT_MSDC2_INS_NUM
		/* control Eint irq */
		if (0 == host_sd->sd_cd_polarity)
			mt_eint_set_polarity(CUST_EINT_MSDC2_INS_NUM,
					MT_POLARITY_LOW);
		else
			mt_eint_set_polarity(CUST_EINT_MSDC2_INS_NUM,
					MT_POLARITY_HIGH);
#endif/*CUST_EINT_MSDC2_INS_NUM*/
	}
#endif

	if (carddet_devices->suspend_flag == 0 &&  got_bad_card == 0) {
		card_info->thread_req = CARDDET_REQ_IRQ;
		wake_up_interruptible(&card_info->thread_wq);
	}

	DBG_MSG("SD card %s(%x:%x)",
		(card_info->host_sd->hw->cd_level ^
		 card_info->host_sd->sd_cd_polarity) ?
			"insert" : "remove",
		 card_info->host_sd->hw->cd_level,
		 card_info->host_sd->sd_cd_polarity);

	return IRQ_HANDLED;
}
static int carddet_eint_handler(int irq, void *arg)
{
	DBG_MSG("** %s called", __func__);
	return carddet_eint_detect(&carddet_devices->card_info[0]);
}

static int carddet_eint_handler1(int irq, void *arg)
{
	DBG_MSG("** %s called", __func__);
	return carddet_eint_detect(&carddet_devices->card_info[1]);
}

/****************/
/*@ MAIN THREAD */
/****************/
static void carddet_card_detect(struct carddet_card_info *card_info)
{
	unsigned long flags;
	int ret;
	struct msdc_host *host_sd;

	for (;;) {
		if (card_info->thread_timeout != 0) {
			/* waiting for events with timeout */
			ret = wait_event_interruptible_timeout(
					 card_info->thread_wq,
					(card_info->thread_req !=
					 CARDDET_REQ_NONE) ||
					 kthread_should_stop(),
					 card_info->thread_timeout);
		} else {
			/* waiting for events */
			ret = wait_event_interruptible(
					 card_info->thread_wq,
					(card_info->thread_req !=
					 CARDDET_REQ_NONE) ||
					 kthread_should_stop());
		}
		if (ret == -ERESTARTSYS)
			continue;

		/* catch a stop event */
		if ((ret < 0) || kthread_should_stop())
			break;

		if (carddet_devices->suspend_flag) {
			card_info->thread_req = CARDDET_REQ_NONE;
			card_info->thread_seq = CARDDET_SEQ_NONE;
			card_info->thread_timeout = CARDDET_TIMEOUT_IDLE;
			card_info->insert_state_flag =
				CARDDET_STATE_FLAG_NONE;

			continue;
		}

		host_sd = card_info->host_sd;
		DBG_MSG("Evnet:sequence:%d, request:%d, timeout:%d",
			card_info->thread_seq,
			card_info->thread_req, ret);

		if (card_info->thread_req == CARDDET_REQ_IRQ) {
			/* Request: Detect/Remove a card
					(from Interrupt handler) */
			if (host_sd->hw->cd_level ^ host_sd->sd_cd_polarity) {
				card_info->thread_seq =
					CARDDET_SEQ_PWRON;
				card_info->thread_timeout =
					CARDDET_TIMEOUT_WAIT_PWR_STABLE;

				spin_lock_irqsave(&card_info->lock, flags);
				card_info->det_state =
						ICX_CARDDET_STATE_INSERTED;
				card_info->read_only = 0;
				card_info->insert_state_flag =
						CARDDET_STATE_FLAG_ALL;
				spin_unlock_irqrestore(&card_info->lock, flags);

				carddet_control_power(card_info);
				card_info->thread_req = CARDDET_REQ_NONE;
			} else {
				carddet_card_removed(card_info);
				tasklet_hi_schedule(&host_sd->card_tasklet);
			}

			card_info->retry_count = 0;
		} else {
			/* Timeout evnet */
			switch (card_info->thread_seq) {
			case CARDDET_SEQ_PWRON:
				carddet_notify_status(carddet_devices);

				/* fall through */

			case CARDDET_SEQ_RETRY:
				/* translate sequence and timer */
				card_info->thread_seq =
					CARDDET_SEQ_SET_PULL;
				card_info->thread_timeout =
					CARDDET_TIMEOUT_WAIT_PULL_STABLE;

				/* get card type */
				spin_lock_irqsave(&card_info->lock,
							flags);
				card_info->card_type =
							ICX_CARDDET_TYPE_SD;
				spin_unlock_irqrestore(&card_info->lock,
							flags);
				break;

			case CARDDET_SEQ_SET_PULL:
				if (enable_req_det_func) {
				    spin_lock_irqsave(
						&card_info->lock, flags);
				    card_info->insert_state_flag &=
						~CARDDET_STATE_FLAG_WAIT_STABLE;

				    if (card_info->insert_state_flag) {
					spin_unlock_irqrestore(
						&card_info->lock, flags);

					/* not ready to detect ->
							keep current state */
					card_info->thread_timeout =
						CARDDET_TIMEOUT_IDLE;
				    } else {
					spin_unlock_irqrestore(
						&card_info->lock, flags);

					card_info->thread_seq =
							CARDDET_SEQ_INIT;
					card_info->thread_timeout =
						CARDDET_TIMEOUT_WAIT_INIT;

					/* initialize SD/MS */
					if (card_info->card_type ==
						ICX_CARDDET_TYPE_SD)
						tasklet_hi_schedule(
						&host_sd->card_tasklet);
				    }
				} else {
					/* translate sequence and timer */
					card_info->thread_seq =
						CARDDET_SEQ_INIT;
					card_info->thread_timeout =
						CARDDET_TIMEOUT_WAIT_INIT;

					/* initialize SD/MS */
					if (card_info->card_type ==
						ICX_CARDDET_TYPE_SD)
						tasklet_hi_schedule(
							&host_sd->card_tasklet);
				}
				break;

			case CARDDET_SEQ_INIT:
				/* get driver data */
				if (carddet_check_result(card_info)) {
					/* translate sequence and timer */
					card_info->thread_seq =
						CARDDET_SEQ_RETRY;
					card_info->thread_timeout =
						CARDDET_TIMEOUT_RETRY;
				} else {
					/* translate sequence and
							timer to init value */
					card_info->thread_seq =
						CARDDET_SEQ_NONE;
					card_info->thread_timeout =
						CARDDET_TIMEOUT_IDLE;
					card_info->retry_count = 0;
				}
				break;

			default:
				/* translate sequence and timer to init value */
				card_info->thread_seq = CARDDET_SEQ_NONE;
				card_info->thread_timeout =
						CARDDET_TIMEOUT_IDLE;

				spin_lock_irqsave(&card_info->lock,
							flags);
				card_info->insert_state_flag =
						CARDDET_STATE_FLAG_NONE;
				spin_unlock_irqrestore(&card_info->lock,
							flags);
				break;
			}

			if (card_info->thread_req != CARDDET_REQ_IRQ)
				card_info->thread_req = CARDDET_REQ_NONE;
		}
	}
}

static int carddet_thread(void *arg)
{
	struct carddet_card_info *card_info;
	card_info = arg;

	carddet_card_detect(card_info);

	return 0;
}

/**************************/
/*@ DRIVER ENTRY ROUTINES */
/**************************/
/* SUSPEND */
static int carddet_suspend(struct platform_device *device, pm_message_t state)
{
	int i;

	DBG_MSG("** %s called", __func__);

	carddet_devices->suspend_flag = 1;
	for (i = 0; i < CARDDET_CARD_MAX_NUM; i++) {
		carddet_devices->card_info[i].prev_state =
			carddet_devices->card_info[i].det_state;
		if (carddet_devices->card_info[i].det_state ==
			ICX_CARDDET_STATE_DETECTED)
			carddet_card_removed(&carddet_devices->card_info[i]);
	}

	return 0;
}

/* RESUME */
static int carddet_resume(struct platform_device *device)
{
	int i;
	DBG_MSG("** %s called", __func__);

	carddet_devices->suspend_flag = 0;
	for (i = 0; i < CARDDET_CARD_MAX_NUM; i++) {
		if (carddet_get_hw_detect_state(
			&carddet_devices->card_info[i])) {
			DBG_MSG("** %s insert", __func__);
			carddet_devices->card_info[i].thread_req =
							CARDDET_REQ_IRQ;
			carddet_devices->card_info[i].thread_seq =
							CARDDET_SEQ_PWRON;
			carddet_devices->card_info[i].retry_count = 0;
			carddet_devices->card_info[i].thread_timeout =
							CARDDET_TIMEOUT_IDLE;
			wake_up_interruptible(
				&carddet_devices->card_info[i].thread_wq);
		} else if ((carddet_devices->card_info[i].prev_state !=
					ICX_CARDDET_STATE_REMOVED) &&
			   (!carddet_get_hw_detect_state(
					&carddet_devices->card_info[i]))) {

			DBG_MSG("** %s remove (notify to user)", __func__);
			carddet_card_removed(&carddet_devices->card_info[i]);
			tasklet_hi_schedule(
			&carddet_devices->card_info[i].host_sd->card_tasklet);
		} else if (carddet_devices->card_info[i].det_state !=
			carddet_devices->card_info[i].prev_state) {
			DBG_MSG("** %s remove (don't notify to user)",
				__func__);
			tasklet_hi_schedule(
			&carddet_devices->card_info[i].host_sd->card_tasklet);
		}
	}

	return 0;
}

/* PROBE */
static int carddet_probe(struct platform_device *device)
{
	dev_t dev = 0;
	int ret;
	int i;
	struct device *carddet_dev = NULL;
	(void)device;

	DBG_MSG("** %s called", __func__);

	if (carddet_major != 0) {
		/* fixed */
		dev = MKDEV(carddet_major, carddet_minor);
		ret = register_chrdev_region(dev, 1, CARDDET_CDEV_FILE_NAME);
		DBG_MSG("### register_chrdev_region carddet: major=%d minor=%d",
			 carddet_major, carddet_minor);
	} else {
		/* dynamic */
		ret = alloc_chrdev_region(&dev, carddet_minor, 1,
					  CARDDET_CDEV_FILE_NAME);
		carddet_major = MAJOR(dev);
		DBG_MSG("### alloc_chrdev_region carddet: major=%d minor=%d",
			 carddet_major, carddet_minor);
	}

	if (ret < 0) {
		ERR_MSG("carddet: can't get major. (%d)", ret);
		return ret;
	}

	/*== device info ==*/
	carddet_devices = kmalloc(sizeof(struct carddet_dev), GFP_KERNEL);
	if (carddet_devices == NULL) {
		ret = -ENOMEM;
		goto err_kmalloc;
	}
	memset(carddet_devices, 0, sizeof(struct carddet_dev));

	/*== initialize parameters ==*/
	for (i = 0; i < CARDDET_CARD_MAX_NUM; i++) {
		spin_lock_init(&carddet_devices->card_info[i].lock);
		init_waitqueue_head(&carddet_devices->card_info[i].thread_wq);
		carddet_devices->card_info[i].retry_count = 0;
		carddet_devices->card_info[i].card_type = ICX_CARDDET_TYPE_SD;
		carddet_devices->card_info[i].thread_timeout =
							CARDDET_TIMEOUT_IDLE;
		carddet_devices->card_info[i].thread_req = CARDDET_REQ_NONE;
		carddet_devices->card_info[i].thread_seq = CARDDET_SEQ_NONE;
		carddet_devices->card_info[i].insert_state_flag =
							CARDDET_STATE_FLAG_NONE;
		carddet_devices->card_info[i].det_state =
						ICX_CARDDET_STATE_REMOVED;
	}

	init_waitqueue_head(&carddet_devices->wqh);
	carddet_devices->users          = 0;
	carddet_devices->suspend_flag   = 0;
#ifdef ENABLE_STAT_QUEUE
	INIT_LIST_HEAD(&carddet_devices->list);
#else  /* ENABLE_STAT_QUEUE */
	carddet_devices->wake_up_flg    = 0;
#endif /* ENABLE_STAT_QUEUE */

	/*== initialize cdev ==*/
	cdev_init(&carddet_devices->cdev, &carddet_fops);
	/* carddet_devices->cdev = cdev_alloc(); */
	carddet_devices->cdev.owner = THIS_MODULE;
	carddet_devices->cdev.ops   = &carddet_fops;

	ret = cdev_add(&carddet_devices->cdev, dev, 1);
	if (ret) {
		ERR_MSG("carddet: error(%d) adding carddet", ret);
		goto err_cdev_add;
	}

	carddet_class = class_create(THIS_MODULE, "icx_carddet");

	if (IS_ERR(carddet_class)) {
		ERR_MSG("carddet: error(%d) class_create", ret);
		goto err_cdev_add;
	}

	carddet_dev = device_create(carddet_class, NULL,
				    dev, NULL, "icx_carddet");
	if (IS_ERR(carddet_dev))
		goto err_cdev_add;

	/*== create task ==*/
	for (i = 0; i < CARDDET_CARD_MAX_NUM; i++) {
		carddet_devices->card_info[i].thread_task = kthread_create(
		carddet_thread, &carddet_devices->card_info[i], MODULE_NAME);
		if (IS_ERR(carddet_devices->card_info[i].thread_task)) {
			ret = PTR_ERR(
				carddet_devices->card_info[i].thread_task);
			ERR_MSG("carddet: error(%d) failed to create a thread",
									ret);
			goto err_create_thread;
		}
		wake_up_process(carddet_devices->card_info[i].thread_task);
	}

	/*== carddet function register ==*/
#if defined(CONFIG_ENABLE_CARDDET)
	msdc_set_carddet_func(&carddet_func);
	msc_bind_carddet(&carddet_to_msc_func_tbl);
#endif /*CONFIG_ENABLE_CARDDET*/

	for (i = 0; i < CARDDET_CARD_MAX_NUM; i++) {
		/* card_info[0].host_sd : msdc_host 1 */
		/* card_info[1].host_sd : msdc_host 2 */
		carddet_devices->card_info[i].host_sd = mtk_msdc_host[i+1];
		carddet_control_power(&carddet_devices->card_info[i]);
	}

	/*== request Eint IRQ ==*/
#if defined(CONFIG_ENABLE_CARDDET)
	msdc_eint_register(&carddet_ops, true, CARDDET_CARD_MAX_NUM);
#endif /*CONFIG_ENABLE_CARDDET*/
	DBG_MSG("carddet: probe end");

	return 0;

	/*== Error Case  ==*/
err_create_thread:
	/* do nothing */

err_cdev_add:
	kfree(carddet_devices);

err_kmalloc:
	unregister_chrdev_region(dev, 1);

	return ret;
}

/* REMOVE */
static int carddet_remove(struct platform_device *device)
{
	dev_t dev;
	int i;
	(void)device;

	DBG_MSG("carddet: remove");

#if defined(CONFIG_ENABLE_CARDDET)
#ifdef CUST_EINT_MSDC1_INS_NUM
	/* disable eint */
	msdc_eint_register(&carddet_ops, false, CARDDET_CARD_MAX_NUM);
#endif /* CUST_EINT_MSDC1_INS_NUM */
#endif /*CONFIG_ENABLE_CARDDET*/
	/* get device file */
	dev = MKDEV(carddet_major, carddet_minor);

	for (i = 0; i < CARDDET_CARD_MAX_NUM; i++)
		kthread_stop(carddet_devices->card_info[i].thread_task);

	if (carddet_devices) {
#ifdef ENABLE_STAT_QUEUE
		struct carddet_detect_status *status, *status_next;
		list_for_each_entry_safe(status, status_next,
					 &carddet_devices->list, entry) {
			list_del(&status->entry);
			kfree(status);
		}
#endif /* ENABLE_STAT_QUEUE */

		device_destroy(carddet_class, dev);
		class_destroy(carddet_class);
		cdev_del(&carddet_devices->cdev);
		kfree(carddet_devices);
	}

	unregister_chrdev_region(dev, 1);

	DBG_MSG("carddet: remove end");
	return 0;
}

/* SHUTDOWN */
static void carddet_shutdown(struct platform_device *device)
{
	DBG_MSG("** %s called", __func__);

	carddet_remove(device);
}

static struct platform_driver carddet_driver = {
	.remove   = carddet_remove,
	.probe    = carddet_probe,
	.suspend  = carddet_suspend,
	.resume   = carddet_resume,
	.shutdown = carddet_shutdown,
	.driver   = {
		.name = MODULE_NAME,
	},
};

/* INIT */
static int __init carddet_init(void)
{
	INFO_MSG("Starting ICX card detect driver.");

	enable_req_det_func = 0;
	carddet_class = NULL;

	return platform_driver_register(&carddet_driver);
}

/* EXIT */
static void __exit carddet_exit(void)
{
	INFO_MSG("Exiting ICX card detect driver.");

	platform_driver_unregister(&carddet_driver);
}

module_init(carddet_init);
module_exit(carddet_exit);

MODULE_AUTHOR("sony corporation");
MODULE_DESCRIPTION("ICX card detect driver");
MODULE_LICENSE("GPL");
/* MODULE_LICENSE("proprietary"); */

