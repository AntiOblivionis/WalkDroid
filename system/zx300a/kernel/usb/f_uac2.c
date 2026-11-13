#include <linux/vmalloc.h>
#include <linux/kref.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/math64.h>
/*
 * Copyright 2016,2017 Sony Corporation
 */
/*
 * f_uac2.c -- USB Audio Class 2.0 Function
 *
 * Copyright (C) 2011
 *    Yadwinder Singh (yadi.brar01@gmail.com)
 *    Jaswinder Singh (jaswinder.singh@linaro.org)
 * Copyright (C) 2017 Mediatek Inc.
 * Copyright 2015,2016,2017 Sony Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/fs.h>

#include <net/netlink.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

/* Playback(USB-IN) Default Stereo - Fl/Fr */
//static int p_chmask = 0x02;
static int p_chmask = 0x00;
module_param(p_chmask, uint, S_IRUGO);
MODULE_PARM_DESC(p_chmask, "Playback Channel Mask");

/* Playback Default 48 KHz */
static int p_srate = 44100;
module_param(p_srate, uint, S_IRUGO);
MODULE_PARM_DESC(p_srate, "Playback Sampling Rate");

/* Playback Default 16bits/sample */
static int p_ssize = 2;
module_param(p_ssize, uint, S_IRUGO);
MODULE_PARM_DESC(p_ssize, "Playback Sample Size(bytes)");

/* Capture(USB-OUT) Default Stereo - Fl/Fr */
static int c_chmask = 0x03;
module_param(c_chmask, uint, S_IRUGO);
MODULE_PARM_DESC(c_chmask, "Capture Channel Mask");

/* Capture Default 64 KHz */
static int c_srate = 44100;
module_param(c_srate, uint, S_IRUGO);
MODULE_PARM_DESC(c_srate, "Capture Sampling Rate");

/* Capture Default 16bits/sample */
static int c_ssize = 4;
module_param(c_ssize, uint, S_IRUGO);
MODULE_PARM_DESC(c_ssize, "Capture Sample Size(bytes)");

/* Keep everyone on toes */
#define USB_XFERS	4
#define QMU_PACKAGE_LENGTH	1024
#define QMU_PACKAGE_NUM	16
#define EXTENT_BUFF_MAX (QMU_PACKAGE_LENGTH * QMU_PACKAGE_NUM * 2)

#define DISP_USAGE 0
#define FB_MAX    5 // 0.0005
#define FB_MID    3 // 0.0003
#define FB_MIN    1 // 0.0001
#define FB_ZERO   0
#define FEED_PLUS FB_MIN
#define FEED_MINUS FB_MIN
#define FEED_THRESH 50 //%
#define FEED_START 180 //ms

extern unsigned int f_plus = FEED_PLUS;
extern unsigned int f_minus = FEED_MINUS;
extern unsigned int f_thresh = FEED_THRESH;
extern unsigned int f_start = FEED_START;

#ifndef CONFIG_USB11_MTK_HDRC
unsigned int wait_return = 0;
#endif /*CONFIG_USB11_MTK_HDRC */
unsigned int f_allow = 1;
static unsigned int f_valid = 1;

/*
 * The driver implements a simple UAC_2 topology.
 * USB-OUT -> IT_1 -> OT_3 -> ALSA_Capture
 * ALSA_Playback -> IT_2 -> OT_4 -> USB-IN
 * Capture and Playback sampling rates are independently
 *  controlled by two clock sources :
 *    CLK_5 := c_srate, and CLK_6 := p_srate
 */
#define USB_OUT_IT_ID	1
#define IO_IN_IT_ID	2
#define IO_OUT_OT_ID	3
#define USB_IN_OT_ID	4
#define USB_OUT_CLK_ID	41
#define USB_IN_CLK_ID	6
#define USB_OUT_CLK_SEL_ID	40
#define USB_FEATURE_UNIT_ID	8

#define CONTROL_ABSENT	0
#define CONTROL_RDONLY	1
#define CONTROL_RDWR	3

#define CLK_FREQ_CTRL	0
#define CLK_VLD_CTRL	2

#define COPY_CTRL	0
#define CONN_CTRL	2
#define OVRLD_CTRL	4
#define CLSTR_CTRL	6
#define UNFLW_CTRL	8
#define OVFLW_CTRL	10

#define DEBUG_DUMPDATA 0
static int dump_buf_size = 4096 * 2;
static struct file *log_filp = NULL;
mm_segment_t log_fs;
static struct net uac2_msg_init;
	
#define FEEDBACK_FUNCTION 1
#define FEEDBACK_BUF_SIZE 4
struct usb_request* FeedBackReq;
extern void reg_uac2_buffer_status(void* pf);

const char *uac2_name = "snd_uac2";

#define UAC2_EVT_MAX_SIZE 1024

char remaindata[8];
char combine_buffer[EXTENT_BUFF_MAX + 8];
int remain_byte = 0;
int gFirstCount = 0;
int gSPEED = 0;

struct uac2_req {
	struct uac2_rtd_params *pp; /* parent param */
	struct usb_request *req;
};

struct uac2_rtd_params {
	bool ep_enabled; /* if the ep is enabled */
	/* Size of the ring buffer */
	size_t dma_bytes;
	unsigned char *dma_area;

	struct snd_pcm_substream *ss;

	/* Ring buffer */
	ssize_t hw_ptr;

	void *rbuf;

	size_t period_size;

	unsigned max_psize;
	struct uac2_req ureq[USB_XFERS];

	spinlock_t lock;
};

//used by feedback
struct uac2_feedback {
	void *pbuf;
	unsigned fb_size;
};

struct uac2_dump_buf {
	u8 *buf;
	int actual;
	struct list_head list;
};

struct uac2_uevent {
	short action;
	short format;
	short bitwidth;
	int freq;
};

struct snd_uac2_chip {
	struct platform_device pdev;
	struct platform_driver pdrv;

	struct uac2_rtd_params p_prm;
	struct uac2_rtd_params c_prm;

	struct snd_card *card;
	struct snd_pcm *pcm;

//	struct uac2_feedback feedback;
	
	struct sock *sock;
	struct uac2_uevent uevent;
	char *msg;

	struct work_struct dump_work;
	struct uac2_dump_buf *dump_buf;
	struct list_head dump_queue;
	spinlock_t			lock;
	
	void *pextent;
};

enum {
	ACTION_PLAY,
	ACTION_STOP,
	ACTION_NONE,	
	FORMAT_PCM,
	FORMAT_DSD,
	FORMAT_DOP,
	FORMAT_NONE,	
	BIT_1,
	BIT_16 = 16,
	BIT_24 = 24,
	BIT_32 = 32,
	BIT_NONE,
	FREQ_32000 = 32000,
	FREQ_44100 = 44100,
	FREQ_48000 = 48000,
	FREQ_88200 = 88200,
	FREQ_96000 = 96000,
	FREQ_176400 = 176400,
	FREQ_192000 = 192000,
	FREQ_352800 = 352800,
	FREQ_384000 = 384000,
	FREQ_2822400 = 2822400,
	FREQ_5644800 = 5644800,
	FREQ_11289600 = 11289600,
	FREQ_NONE,
};

enum {
	EVENT_ACTION_PLAY,
	EVENT_ACTION_STOP,
	EVENT_ACTION_NONE,
	EVENT_FORMAT_PCM,
	EVENT_FORMAT_DSD,
	EVENT_FORMAT_DOP,
	EVENT_FORMAT_NONE,	
	EVENT_BIT_1,
	EVENT_BIT_16,
	EVENT_BIT_24,
	EVENT_BIT_32,
	EVENT_BIT_NONE,
	EVENT_FREQ_32000,
	EVENT_FREQ_44100,
	EVENT_FREQ_48000,
	EVENT_FREQ_88200,
	EVENT_FREQ_96000,
	EVENT_FREQ_176400,
	EVENT_FREQ_192000,
	EVENT_FREQ_352800,
	EVENT_FREQ_384000,
	EVENT_FREQ_2822400,
	EVENT_FREQ_5644800,
	EVENT_FREQ_11289600,
	EVENT_FREQ_NONE,
};

static struct usb_string UAC2EVENT[] = {
	[EVENT_ACTION_PLAY].s = "ACTION=PLAY\n",
	[EVENT_ACTION_STOP].s = "ACTION=STOP\n",
	[EVENT_ACTION_NONE].s = "ACTION=NONE\n",
	[EVENT_FORMAT_PCM].s = "FORMAT=PCM\n",
	[EVENT_FORMAT_DSD].s = "FORMAT=DSD\n",
	[EVENT_FORMAT_DOP].s = "FORMAT=DOP\n",
	[EVENT_FORMAT_NONE].s = "FORMAT=NONE\n",
	[EVENT_BIT_1].s = "BITWIDTH=1\n",
	[EVENT_BIT_16].s = "BITWIDTH=16\n",
	[EVENT_BIT_24].s = "BITWIDTH=24\n",
	[EVENT_BIT_32].s = "BITWIDTH=32\n",
	[EVENT_BIT_NONE].s = "BITWIDTH=NONE\n",
	[EVENT_FREQ_32000].s = "FREQ=32000\n",
	[EVENT_FREQ_44100].s = "FREQ=44100\n",
	[EVENT_FREQ_48000].s = "FREQ=48000\n",
	[EVENT_FREQ_88200].s = "FREQ=88200\n",
	[EVENT_FREQ_96000].s = "FREQ=96000\n",
	[EVENT_FREQ_176400].s = "FREQ=176400\n",
	[EVENT_FREQ_192000].s = "FREQ=192000\n",
	[EVENT_FREQ_352800].s = "FREQ=352800\n",
	[EVENT_FREQ_384000].s = "FREQ=384000\n",
	[EVENT_FREQ_2822400].s = "FREQ=2822400\n",
	[EVENT_FREQ_5644800].s = "FREQ=5644800\n",
	[EVENT_FREQ_11289600].s = "FREQ=11289600\n",
	[EVENT_FREQ_NONE].s = "FREQ=NONE\n",
};

short uac2_event_map(int item)
{
	switch (item)
	{
		case ACTION_PLAY:		return EVENT_ACTION_PLAY; 
		case ACTION_STOP:		return EVENT_ACTION_STOP; 
		case ACTION_NONE:		return EVENT_ACTION_NONE; 
		case FORMAT_PCM:		return EVENT_FORMAT_PCM;  
		case FORMAT_DSD:		return EVENT_FORMAT_DSD;  
		case FORMAT_DOP:		return EVENT_FORMAT_DOP;  
		case FORMAT_NONE:		return EVENT_FORMAT_NONE;	
		case BIT_1:				return EVENT_BIT_1;	  
		case BIT_16:			return EVENT_BIT_16;	  
		case BIT_24:			return EVENT_BIT_24;	  
		case BIT_32:			return EVENT_BIT_32;	  
		case BIT_NONE:			return EVENT_BIT_NONE;	  
		case FREQ_32000:		return EVENT_FREQ_32000;  
		case FREQ_44100:		return EVENT_FREQ_44100;  
		case FREQ_48000:		return EVENT_FREQ_48000;  
		case FREQ_88200:		return EVENT_FREQ_88200;  
		case FREQ_96000:		return EVENT_FREQ_96000;  
		case FREQ_176400:		return EVENT_FREQ_176400; 
		case FREQ_192000:		return EVENT_FREQ_192000; 
		case FREQ_352800:		return EVENT_FREQ_352800; 
		case FREQ_384000:		return EVENT_FREQ_384000; 
		case FREQ_2822400:		return EVENT_FREQ_2822400; 
		case FREQ_5644800:		return EVENT_FREQ_5644800; 
		case FREQ_11289600:		return EVENT_FREQ_11289600;
		case FREQ_NONE: 		return EVENT_FREQ_NONE;

		default: 
			return EVENT_FREQ_NONE;
	}
}


#define BUFF_SIZE_MAX	(PAGE_SIZE * 1024)
#define PRD_SIZE_MAX	(PAGE_SIZE * 8)
#define MIN_PERIODS	4

#define DSD_ALT 7

//for dsd/dop 
#define DOP_DETECT_PCM          (0)
#define DOP_DETECT_DOP          (1)
#define DOP_DETECT_ERR_SIZEOVER (-1)

#define DOP_LOWER_MARKER        0x05
#define DOP_UPPER_MARKER        0xFA
#define DOP_XOR_MARKER          0xFF

#define DOP_MARKER_MASK         0xFF000000
#define DOP_MARKER_CLEAR        0
#define DOP_CONT_CLEAR          0
#define DOP_CONT_SET            32


#ifdef CONFIG_ICX_USE_BID
#include <mach/icx_pm_helper.h>
static int check_max_rate(int format_type)
{
	switch (icx_pm_helper_bid2)
	{
		case ICX_BID2_1277:
		case ICX_BID2_1288:
		case ICX_BID2_1300:
			/* pr_debug("BID=ICX_BID2_1277\n"); */
			if (format_type == FORMAT_DSD)
				return FREQ_11289600; //DSD256
			else
				return FREQ_384000;  //PCM384

			break;

		case ICX_BID2_1280: /* same to ICX1291 */
		case ICX_BID2_1290:
			/* pr_debug("BID=ICX_BID2_1280\n"); */
			if (format_type == FORMAT_DSD)
				return FREQ_5644800; //DSD128
			else
				return FREQ_192000;  //PCM192

			break;

		default:
			pr_debug("BID=UNKOWN\n");
			if (format_type == FORMAT_DSD)
				return FREQ_5644800; //DSD128
			else
				return FREQ_192000;  //PCM192

	}
}
#endif

static void uac2_netlink_rcv(struct sk_buff *__skb)
{
	pr_debug("[UAC] uac2_netlink_rcv called @Line %d!\n", __LINE__);
	return;
}

static struct sock * uac2_netlink_init(void)
{	
	struct sock *uac2sock;
	struct netlink_kernel_cfg cfg = {
		.input	= uac2_netlink_rcv,
	};

	uac2sock = netlink_kernel_create(&init_net, NETLINK_UAC2, &cfg);
	if (!uac2sock)
	{
		pr_err("[UAC] create fail @Line %d!\n", __LINE__);
		return NULL;
	}
	
	pr_debug("[UAC] create netlink socket(%p) successful @Line %d!\n", uac2sock, __LINE__);

	return uac2sock;
}

void uac2_netlink_exit(struct sock *sock)
{
	netlink_kernel_release(sock);
}

int uac2_netlink_send(struct sock *sock, void *msg, int len, u32 sequence)
{
struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	int ret = 0;

	skb = nlmsg_new(len, GFP_ATOMIC);
	if (!skb) 
	{
		pr_err("[UAC] nlmsg_new fail @Line %d!\n", __LINE__);
		ret = -1;
		goto exit;
	}
	
	nlh = nlmsg_put(skb, 0, sequence, 0, len, 0);
	if (!nlh) {
		kfree_skb(skb);
		pr_err("[UAC] nlmsg_put fail @Line %d!\n", __LINE__);
		ret = -2;
		goto exit;
	}
	memcpy(nlmsg_data(nlh), msg, len);

	NETLINK_CB(skb).portid = 0;
	NETLINK_CB(skb).dst_group = 0;

	
	ret = netlink_broadcast(sock, skb, 0, 1, GFP_ATOMIC);
	if (0 != ret)
	{
		pr_err("[UAC] send fail(%d) socket(%p) @Line %d!\n", ret, sock, __LINE__);
	}
exit:	
	return ret;
}

static void print_umsg(struct uac2_uevent *msg, int func_line)
{

	short action = uac2_event_map(msg->action);
	short format = uac2_event_map(msg->format);
	short freq = uac2_event_map(msg->freq);
	short bitwidth = uac2_event_map(msg->bitwidth);
	char *pmsg_buff = kzalloc(UAC2_EVT_MAX_SIZE, GFP_KERNEL);

	if (!pmsg_buff) {
		pr_info("No Memory for Display ACTION=PLAY @Line %d!\n", func_line);
		return;
	}

	strncat(pmsg_buff, "[UAC] ", 6);
	strncat(pmsg_buff, UAC2EVENT[action].s, strlen(UAC2EVENT[action].s)-1);
	strncat(pmsg_buff, ", ", 2);
	strncat(pmsg_buff, UAC2EVENT[format].s, strlen(UAC2EVENT[format].s)-1);
	strncat(pmsg_buff, ", ", 2);
	strncat(pmsg_buff, UAC2EVENT[freq].s, strlen(UAC2EVENT[freq].s)-1);
	strncat(pmsg_buff, ", ", 2);
	strncat(pmsg_buff, UAC2EVENT[bitwidth].s, strlen(UAC2EVENT[bitwidth].s)-1);
	strncat(pmsg_buff, " ", 1);
	pr_info("%s @Line %d!\n", pmsg_buff, func_line);

	kfree(pmsg_buff);

	return;
}

char *uac2_create_uevent_msg(struct snd_uac2_chip *uac2, struct uac2_uevent *msg, int *size)
{
	short action = uac2_event_map(msg->action);
	short format = uac2_event_map(msg->format);
	short freq = uac2_event_map(msg->freq);
	short bitwidth = uac2_event_map(msg->bitwidth);

	uac2->msg = kzalloc(UAC2_EVT_MAX_SIZE, GFP_KERNEL);

	if (!uac2->msg) {
		*size = 0;
		pr_info("No Memory for create msg ACTION=PLAY\n");
		return uac2->msg;
	}

	strncat(uac2->msg, UAC2EVENT[action].s, strlen(UAC2EVENT[action].s));
	strncat(uac2->msg, UAC2EVENT[format].s, strlen(UAC2EVENT[format].s));
	strncat(uac2->msg, UAC2EVENT[freq].s, strlen(UAC2EVENT[freq].s));
	strncat(uac2->msg, UAC2EVENT[bitwidth].s, strlen(UAC2EVENT[bitwidth].s));
	
	*size = strlen(UAC2EVENT[action].s) + strlen(UAC2EVENT[format].s) 
		   + strlen(UAC2EVENT[freq].s) + strlen(UAC2EVENT[bitwidth].s);

	return uac2->msg;
}

short uac2_get_uevent_status(struct snd_uac2_chip *uac2)
{
	struct uac2_uevent *u2evt = &(uac2->uevent);
	return u2evt->action;
}

short uac2_get_uevent_format(struct snd_uac2_chip *uac2)
{
	struct uac2_uevent *u2evt = &(uac2->uevent);
	return u2evt->format;
}

void uac2_uevent_get_msg(struct snd_uac2_chip *uac2, struct uac2_uevent *msg)
{
	struct uac2_uevent *u2evt = &(uac2->uevent);

	msg->action = u2evt->action;
	msg->format = u2evt->format;
	msg->freq = u2evt->freq;
	msg->bitwidth = u2evt->bitwidth;
		
	return;
}

void uac2_uevent_store_msg(struct snd_uac2_chip *uac2, struct uac2_uevent *msg)
{
	struct uac2_uevent *u2evt = &(uac2->uevent);

	u2evt->action = msg->action;
	u2evt->format = msg->format;
	u2evt->freq = msg->freq;
	u2evt->bitwidth = msg->bitwidth;
		
	return;
}

void uac2_init_msg(struct snd_uac2_chip *uac2)
{
	struct uac2_uevent *u2evt = &(uac2->uevent);

	u2evt->action = ACTION_STOP;
	u2evt->format = FORMAT_NONE;
	u2evt->freq = FREQ_NONE;
	u2evt->bitwidth = BIT_NONE;
		
	return;
}

void uac2_release_uevent_msg(struct snd_uac2_chip *uac2)
{
	kfree(uac2->msg);
}

struct audio_dev {
	/* Each USB request has its own contiguous DMA payload, never order-4. */
	void *wd_capture_bufs[USB_XFERS];
    struct wd_uac_source *wd_source;
    struct kref wd_refs;
    struct work_struct wd_cleanup_work;
    spinlock_t wd_process_lock;
    unsigned wd_closing,wd_alsa_initialized,wd_requests,wd_bus_refs,wd_pdev_initialized;
    struct completion wd_platform_released;
    u64 wd_clock_generation;
	u8 ac_intf, ac_alt;
	u8 as_out_intf, as_out_alt;
	u8 as_in_intf, as_in_alt;

	struct usb_ep *in_ep, *out_ep;
	struct usb_function func;

	/* The ALSA Sound Card it represents on the USB-Client side */
	struct snd_uac2_chip uac2;
};

static struct audio_dev *agdev_g;
static void wd_source_event(struct audio_dev *,const struct uac2_uevent *,u32,unsigned,int);
static void wd_source_error(struct wd_uac_source *,int);
#define WALKDROID_UAC_BOUNDARY_SOURCE_INTEGRATION_V1 1
#include "sony_uac2_capture_boundary.inc"

bool uac2_uevent_msg_is_change(struct snd_uac2_chip *uac2, struct uac2_uevent *msg)
{
return !(uac2->uevent.action==msg->action&&uac2->uevent.format==msg->format&&
             uac2->uevent.freq==msg->freq&&uac2->uevent.bitwidth==msg->bitwidth);
}

bool uac2_uevent_msg_create(struct snd_uac2_chip *uac2, struct uac2_uevent *msg_event, short alt, short tformat, short tbitwidth, short tslot)
{
bool msg_send = false;
	
	msg_event->action = ACTION_PLAY;
	msg_event->freq = c_srate;
	
	if ((DSD_ALT == alt) && (gSPEED == USB_SPEED_HIGH)) {
		msg_event->format = FORMAT_DSD; 
	}else
	if(DOP_DETECT_DOP == tformat)
	{
		msg_event->format = FORMAT_DOP; 
		msg_event->freq = c_srate/2;
	}else
	{
		msg_event->format = FORMAT_PCM; 
	}
	

	if (FORMAT_PCM == msg_event->format)
	{
		msg_event->bitwidth = tbitwidth;
		
	}else
	{

		switch (msg_event->freq)
		{
			case FREQ_88200:	msg_event->freq = FREQ_2822400;
								break;
			case FREQ_176400:	msg_event->freq = FREQ_5644800;
								break;
			case FREQ_352800:	msg_event->freq = FREQ_11289600;
								break;
		}

		msg_event->bitwidth = BIT_1;
	}

	msg_send = wd_uac_publish(uac2,msg_event,SONY_USB_BOUNDARY_CHANGE)==0;
	return msg_send;
}


static struct snd_pcm_hardware uac2_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER
		 | SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID
		 | SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.rates = SNDRV_PCM_RATE_CONTINUOUS,
	.periods_max = BUFF_SIZE_MAX / PRD_SIZE_MAX,
	.buffer_bytes_max = BUFF_SIZE_MAX,
	.period_bytes_max = PRD_SIZE_MAX,
	.periods_min = MIN_PERIODS,
};



static inline
struct audio_dev *func_to_agdev(struct usb_function *f)
{
	return container_of(f, struct audio_dev, func);
}

static inline
struct audio_dev *uac2_to_agdev(struct snd_uac2_chip *u)
{
	return container_of(u, struct audio_dev, uac2);
}

static inline
struct snd_uac2_chip *pdev_to_uac2(struct platform_device *p)
{
	return container_of(p, struct snd_uac2_chip, pdev);
}

static inline
struct snd_uac2_chip *prm_to_uac2(struct uac2_rtd_params *r)
{
	struct snd_uac2_chip *uac2 = container_of(r,
					struct snd_uac2_chip, c_prm);

	if (&uac2->c_prm != r)
		uac2 = container_of(r, struct snd_uac2_chip, p_prm);

	return uac2;
}

static inline
uint num_channels(uint chanmask)
{
	uint num = 0;

	while (chanmask) {
		num += (chanmask & 1);
		chanmask >>= 1;
	}

	return num;
}

static struct uac2_dump_buf *uac2_dump_buffer_alloc(int buf_size)
{
	struct uac2_dump_buf *copy_buf;

	copy_buf = kzalloc(sizeof(struct uac2_dump_buf), GFP_KERNEL);
	if (!copy_buf)
		return ERR_PTR(-ENOMEM);

	copy_buf->buf = kzalloc(buf_size, GFP_KERNEL);
	if (!copy_buf->buf) {
		kfree(copy_buf);
		return ERR_PTR(-ENOMEM);
	}

	pr_debug("[UAC] buffer alloc pointer(%p) buffer (%p) @Line %d!\n", copy_buf, copy_buf->buf, __LINE__);

	return copy_buf;
}

static void uac2_dump_buffer_free(struct uac2_dump_buf *dump_buf)
{
	pr_debug("[UAC] buffer free pointer(%p) buffer (%p) @Line %d!\n", dump_buf, dump_buf->buf, __LINE__);
	kfree(dump_buf->buf);
	kfree(dump_buf);
}

static void uac2_dump_data_work(struct work_struct *data)
{
	struct snd_uac2_chip *uac2 = container_of(data, struct snd_uac2_chip,
					dump_work);
	struct uac2_dump_buf *dump_buf;

	spin_lock_irq(&uac2->lock);
	if (list_empty(&uac2->dump_queue)) {
		spin_unlock_irq(&uac2->lock);
		return;
	}
	dump_buf = list_first_entry(&uac2->dump_queue,
			struct uac2_dump_buf, list);
	list_del(&dump_buf->list);
	spin_unlock_irq(&uac2->lock);

	log_fs = get_fs();
	set_fs(KERNEL_DS);
	log_filp->f_op->write(log_filp, dump_buf->buf, dump_buf->actual, &(log_filp->f_pos));
	set_fs(log_fs);
	sys_sync();
	
	uac2_dump_buffer_free(dump_buf);
}

static void
uac2_qmu_mem(unsigned char *dst, struct usb_request *req)
{
int j;
	unsigned int dstoffset = 0;
	unsigned char *tmp;
	unsigned char *src;

#if QMU_PACKAGE_NUM
	//becasue use request buffer, so first package don't need copy
	for(j = 1; j < req->number_of_packets; j++)
	{
		dstoffset += req->iso_frame_desc[(j - 1)].actual_length;
		tmp = dst + dstoffset;
		src = req->buf + req->iso_frame_desc[j].offset;
		memmove(tmp, src, req->iso_frame_desc[j].actual_length);
	}
#else
	return;
#endif
}

static unsigned char dsd_marker_check( unsigned char ch0_marker, unsigned char ch1_marker )
{
  unsigned char result = false;
  static  unsigned char prev_marker_ch[2];

  /* DSD Marker check */
  if( (ch0_marker != DOP_LOWER_MARKER && ch0_marker != DOP_UPPER_MARKER)    /* DSD64 Section1 */
   || (ch1_marker != DOP_LOWER_MARKER && ch1_marker != DOP_UPPER_MARKER)    /* DSD64 Section1 */
  ){
    prev_marker_ch[0] = DOP_MARKER_CLEAR;
    prev_marker_ch[1] = DOP_MARKER_CLEAR;
    return( result );
  }
  /* Previous Marker and  Current Maker check */
  if( ( ch0_marker ^ prev_marker_ch[0] ) != DOP_XOR_MARKER
   || ( ch1_marker ^ prev_marker_ch[1] ) != DOP_XOR_MARKER
  ){
    /* ch0 xor ch1 is not 0xFF */
    prev_marker_ch[0] = ch0_marker;
    prev_marker_ch[1] = ch1_marker;
    return( result );
  }
  
  /* DSD Marker is available */
  result = true;
  prev_marker_ch[0] = ch0_marker;
  prev_marker_ch[1] = ch1_marker;
  return( result );
}

short tformat = DOP_DETECT_ERR_SIZEOVER;
short last_tformat = DOP_DETECT_ERR_SIZEOVER;

short format_detect( unsigned char* PcmBufAddr, unsigned short Size )
{
	short detect = last_tformat;
	unsigned short offset, i;
	unsigned int *ppcm_data32, pcm_data32;
	unsigned int zero_data32;
	unsigned char ch0_mkr, ch1_mkr;
	static unsigned char dop_continuous;
	static unsigned int pcm_continuous;

	offset = (sizeof(unsigned int) * 2);

	ppcm_data32 = (unsigned int *)PcmBufAddr;

	for (i = 0; i < Size; i += offset) {
		/* copy buffer to zero_data buffer */
		zero_data32 = *ppcm_data32;

		/* ch0 marker */
		pcm_data32 = (*ppcm_data32 & DOP_MARKER_MASK);
		pcm_data32 >>= 24;
		ch0_mkr = (unsigned char)pcm_data32;
		ppcm_data32++;

		/* copy buffer to zero_data buffer */
		zero_data32 |= *ppcm_data32;

		/* ch1 marker */
		pcm_data32 = (*ppcm_data32 & DOP_MARKER_MASK);
		pcm_data32 >>= 24;
		ch1_mkr = (unsigned char)pcm_data32;
		ppcm_data32++;

		if (dsd_marker_check(ch0_mkr, ch1_mkr) == true) {
			pcm_continuous = 0;
			if (dop_continuous < DOP_CONT_SET) {
				dop_continuous++;
			} else {
				detect = DOP_DETECT_DOP;
			}
		} else {
			dop_continuous = 0;
			/* break if data is PCM */
			if (zero_data32 != 0) {
				if ((ch0_mkr != DOP_LOWER_MARKER && ch0_mkr != DOP_UPPER_MARKER)    /* DSD64 Section1 */
				 || (ch1_mkr != DOP_LOWER_MARKER && ch1_mkr != DOP_UPPER_MARKER)    /* DSD64 Section1 */
				){
					pr_debug("[UAC] zero_data32 = 0x%08X, pcm_continuous = %d, dop_continuous = %d\n",
						zero_data32, pcm_continuous, dop_continuous);
					detect = DOP_DETECT_PCM;
					pcm_continuous = 0;
					break;
				}
			}

			if (pcm_continuous < c_srate) {
				pcm_continuous++;
			} else {
				detect = DOP_DETECT_PCM;
			}
		}
	}

	last_tformat = detect;

	return detect;
}

short remove_marker(unsigned int *ppcm_32bit, unsigned int *pdsd_32bit, unsigned short pbuf_size)
{
  unsigned short offset, pcmdata_end_size, i;
  unsigned int dsd_ch0_buf, dsd_ch1_buf;
  unsigned int *pdsd_ch0_32bit, *pdsd_ch1_32bit;
  unsigned char ch0_marker, ch1_marker;

  pdsd_ch0_32bit = pdsd_32bit;
  pdsd_ch1_32bit = pdsd_32bit + 1;

  offset = (sizeof( unsigned int ) * 4);
  pcmdata_end_size = pbuf_size;
  
  for( i = 0; i < pcmdata_end_size; i += offset ){ 
    /* 1st sample Data(4byte) : CH0 */
	ch0_marker = *ppcm_32bit >> 24;

    dsd_ch0_buf = ((*ppcm_32bit & 0x0000FF00));
    dsd_ch0_buf |= ((*ppcm_32bit & 0x00FF0000) >> 16);
    ppcm_32bit ++;

    /* 2nd sample Data(4byte) : CH1 */
	ch1_marker = *ppcm_32bit >> 24;

    dsd_ch1_buf = ((*ppcm_32bit & 0x0000FF00));
    dsd_ch1_buf |= ((*ppcm_32bit & 0x00FF0000) >> 16);
    ppcm_32bit ++;
    
	if ((ch0_marker != DOP_LOWER_MARKER && ch0_marker != DOP_UPPER_MARKER)    /* DSD64 Section1 */
	 || (ch1_marker != DOP_LOWER_MARKER && ch1_marker != DOP_UPPER_MARKER)    /* DSD64 Section1 */
	){
		dsd_ch0_buf = 0x96969696;
		dsd_ch1_buf = 0x96969696;
		pr_debug("[UAC] ch0_mkr = %02x, c1_mkr = %02x\n", ch0_marker, ch1_marker);
		pr_debug("[UAC] fill no sound DSD data  @Line %d!\n", __LINE__);
	    ppcm_32bit += 2;
	} else {
	    /* 3rd sample Data(4byte) : CH0 */
		ch0_marker = *ppcm_32bit >> 24;

	    dsd_ch0_buf |= ((*ppcm_32bit & 0x0000FF00) << 16);
	    dsd_ch0_buf |= ((*ppcm_32bit & 0x00FF0000));
	    ppcm_32bit++;

	    /* 4th sample Data(4byte) : CH1 */
		ch1_marker = *ppcm_32bit >> 24;

	    dsd_ch1_buf |= ((*ppcm_32bit & 0x0000FF00) << 16);
	    dsd_ch1_buf |= ((*ppcm_32bit & 0x00FF0000));
	    ppcm_32bit++;

		if ((ch0_marker != DOP_LOWER_MARKER && ch0_marker != DOP_UPPER_MARKER)    /* DSD64 Section1 */
		 || (ch1_marker != DOP_LOWER_MARKER && ch1_marker != DOP_UPPER_MARKER)    /* DSD64 Section1 */
		){
			dsd_ch0_buf = (dsd_ch0_buf & 0x0000FFFF) | 0x96960000;
			dsd_ch1_buf = (dsd_ch1_buf & 0x0000FFFF) | 0x96960000;
			pr_debug("[UAC] ch0_mkr = %02x, c1_mkr = %02x\n", ch0_marker, ch1_marker);
			pr_debug("[UAC] fill no sound DSD data  @Line %d!\n", __LINE__);
		}
	}
    *pdsd_ch0_32bit = dsd_ch0_buf;
    *pdsd_ch1_32bit = dsd_ch1_buf;
    pdsd_ch0_32bit +=2;
    pdsd_ch1_32bit +=2;
  }
  return 0;
}

size_t memcpy_pcm32(void *dst, const void *src, size_t size, u32 src_bitdepth) 
{ 
	u32 temp;          
	u32 remain_size;  //in byte
	if (!dst || !src) { // log and return 0;
	}
	if (!size) { // log and return 0; 
	}
	remain_size = size;
	switch (src_bitdepth) 
	{
		case 8: while (remain_size >= 1) 
			{
				*(u32 *)dst = ((u32)(*((u8 *)src)))<<24;
				src += 1;
				dst += 4;
				remain_size -= 1; 
			}
			break; 
		case 16: 
			while (remain_size >= 2) 
			{
				*(u32 *)dst = ((u32)(*((u16 *)src)))<<16;
				src += 2;
				dst += 4;
				remain_size -= 2; 
			}
			break;
		case 24: 
			{ 
				u32 read_idx = 0;
				while (remain_size >= 3) 
				{
					*(u32 *)dst = (((u32)(*((u8 *)(src))))<<8) | 
  							      (((u32)(*((u8 *)(src+1))))<<16) |
  								  (((u32)(*((u8 *)(src+2))))<<24); 
					src += 3; 
					dst += 4;
					remain_size -= 3; 
				} 
			}
			break;
		case 32:
		default: 
			//memcpy(dst, src, size);
			remain_size = 0;
			break; 
		}
	return  ((size-remain_size)  * 32 / src_bitdepth);  // return copied data size in byte 
} 

size_t dsd_deal_data(char *dst, char *src, size_t size) 
{ 
	u32 tmp[8];  
	u32 i;  //in byte
	if (!dst || !src) { // log and return 0;
	}
	if (!size) { // log and return 0; 
	}
	
	for (i = 0; i < size; i = i + 8)
	{
		tmp[0] = src[0 + i];
		tmp[1] = src[4 + i];
		tmp[2] = src[1 + i];
		tmp[3] = src[5 + i];
		tmp[4] = src[2 + i];
		tmp[5] = src[6 + i];
		tmp[6] = src[3 + i];
		tmp[7] = src[7 + i];
		dst[0 + i] = tmp[0];
		dst[1 + i] = tmp[1];
		dst[2 + i] = tmp[2];
		dst[3 + i] = tmp[3];
		dst[4 + i] = tmp[4];
		dst[5 + i] = tmp[5];
		dst[6 + i] = tmp[6];
		dst[7 + i] = tmp[7];
	}
	
	return  size;
} 

extern struct uac2_format_type_i_descriptor as_out_fmt1_desc;
extern struct uac2_format_type_i_descriptor as_out_fmt2_desc;
extern struct uac2_format_type_i_descriptor as_out_fmt3_desc;
extern struct uac2_format_type_i_descriptor as_out_fmt4_desc;
extern struct uac2_format_type_i_descriptor as_out_fmt5_desc;
extern struct uac2_format_type_i_descriptor as_out_fmt6_desc;
extern struct uac2_format_type_i_descriptor as_out_fmt7_desc;

static struct uac2_format_type_i_descriptor *AltDes[] = {
	NULL,
	&as_out_fmt1_desc,
	&as_out_fmt2_desc,
	&as_out_fmt3_desc,		
	&as_out_fmt4_desc,
	&as_out_fmt5_desc,
	&as_out_fmt6_desc,		
	&as_out_fmt7_desc,	
	NULL,
};

#define MAX_ALT_NUM ((sizeof(AltDes)/sizeof(struct uac2_format_type_i_descriptor *)) - 2)

#include "sony_uac2_source_collect.inc"

static void
wd_agdev_iso_process(struct usb_ep *ep, struct usb_request *req)
{
unsigned pending;
	unsigned long flags;
	bool update_alsa = false;
	unsigned char *src, *dst, *tmp;
	int status = req->status;
    unsigned long long u8Pos = 0;
	struct uac2_req *ur = req->context;
	struct snd_pcm_substream *substream;
	struct uac2_rtd_params *prm = ur->pp;
	struct snd_uac2_chip *uac2 = prm_to_uac2(prm);
	struct uac2_dump_buf *dump_buf = uac2->dump_buf;
	struct audio_dev *agdev = container_of(uac2, struct audio_dev, uac2);
	struct uac2_uevent msgevent;
	short alt = agdev->as_out_alt, tslot,  tbitwidth;
	int j;
	unsigned tmpptr = 0;
	int event_created = 0, boundary_rc = 0;

	if(wd_uac_is_quiescing())return;
	/* i/f shutting down */
	if (!prm->ep_enabled)
		return;

	/* pr_debug("[UAC] agdev_iso_complete called select alt: %d\n", alt); */

	/*
	 * We can't really do much about bad xfers.
	 * Afterall, the ISOCH xfers could fail legitimately.
	 */
	if (status)
		pr_debug("%s: iso_complete status(%d) %d/%d\n",
			__func__, status, req->actual, req->length);


    if(status){wd_uac_fail(status);return;}
    if(alt<1||alt>MAX_ALT_NUM||!AltDes[alt]){wd_uac_fail(-EPROTO);return;}
    substream = prm->ss;
	tslot = AltDes[alt]->bSubslotSize;

    tbitwidth = AltDes[alt]->bBitResolution;
    /* Raw bytes must describe complete stereo samples before conversion. */
    if(!req->buf||tslot<1||tslot>4||req->actual>req->length||req->actual%(2*tslot)){
        wd_uac_fail(-EPROTO);return;
    }
#if QMU_PACKAGE_NUM
    {
        unsigned n,total=0,end=0;
        if(req->number_of_packets<1||req->number_of_packets>QMU_PACKAGE_NUM||!req->iso_frame_desc){wd_uac_fail(-EPROTO);return;}
        for(n=0;n<req->number_of_packets;++n){
            struct usb_iso_packet_descriptor *d=&req->iso_frame_desc[n];
            if(d->status||d->offset<end||d->offset>req->length||d->length>req->length-d->offset||
               d->actual_length>d->length||d->actual_length>req->length-total||
               d->actual_length%(2*tslot)||(!n&&d->offset)){
                wd_uac_fail(d->status<0?d->status:-EPROTO);return;
            }
            total+=d->actual_length;
            end=d->offset+d->length;
        }
        if(total!=req->actual){wd_uac_fail(-EPROTO);return;}
    }
#endif
    wd_source_save_packet(agdev,req,tslot);
    if(!req->actual)goto requeue_only;

	/* Do nothing if ALSA isn't active */
	if (!substream)
	{
		/* pr_debug("[UAC] substream not set\n"); */
		if (prm->dma_area)
			pr_debug("[UAC] substream not set but prm->dma_area is not NULL\n");

		if ((3 == tslot) || (4 == tslot))
		{
			tmp = req->buf;
			uac2_qmu_mem(tmp, req);
			src = tmp;
			if(3 == tslot)
			{
				/* extent to 32 bit. must (EXTENT_BUFF_MAX => (req->actual * 4 / 3)) */
				if ((EXTENT_BUFF_MAX * 3) < (req->actual * 4)) {
					WARN_ON(1);
					dev_err(&uac2->pdev.dev, "%s:%d Error! req->actual(%d) is Big\n",
							 __func__, __LINE__, req->actual);
					dev_err(&uac2->pdev.dev, "Reduce req->actual, fit to EXTENT_BUFF_MAX\n");
					req->actual = EXTENT_BUFF_MAX * 3 / 4;
				}
				req->actual = memcpy_pcm32(uac2->pextent, src, req->actual, 24);
				src = uac2->pextent;				
			}
			tformat = format_detect(src, req->actual);
			/* pr_debug("[UAC] format %d!\n",tformat); */
		}
	} else {
		int open_format = 0;
		spin_lock_irqsave(&prm->lock, flags);
		if ((prm->ss == NULL) || (prm->dma_area == NULL) || (prm->dma_bytes == 0) || (prm->period_size == 0)) {
			dev_err(&uac2->pdev.dev, "%s:%d Error!hw_free is called\n", __func__, __LINE__);
			spin_unlock_irqrestore(&prm->lock, flags);
			goto skip_copy;
		} else {

			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
				src = prm->dma_area + prm->hw_ptr;
				req->actual = req->length;
				dst = req->buf;
			} else {
				dst = prm->dma_area + prm->hw_ptr;
				tmp = req->buf;
				uac2_qmu_mem(tmp, req);
				src = tmp;
			}

			pending = prm->hw_ptr % prm->period_size;
			pending += req->actual;
			if (pending >= prm->period_size)
				update_alsa = true;
		}

        open_format = substream->runtime->format;
		pr_debug("[UAC] open_format %d\n", open_format);

		spin_unlock_irqrestore(&prm->lock, flags);
		
		if ((DSD_ALT == alt) && (gSPEED == USB_SPEED_HIGH)) {
			/* DSD data */
			/* do nothing */
		} else {
			switch(tslot)
			{
				case 3:
					/* extent to 32 bit. must (EXTENT_BUFF_MAX => (req->actual * 4 / 3)) */
					if ((EXTENT_BUFF_MAX * 3) < (req->actual * 4)) {
						WARN_ON(1);
						dev_err(&uac2->pdev.dev, "%s:%d Error! req->actual(%d) is Big\n",
								 __func__, __LINE__, req->actual);
						dev_err(&uac2->pdev.dev, "Reduce req->actual, fit to EXTENT_BUFF_MAX\n");
						req->actual = EXTENT_BUFF_MAX * 3 / 4;
					}
					req->actual = memcpy_pcm32(uac2->pextent, src, req->actual, 24);
					src = uac2->pextent;

					// No Need break, case 3 go through the case 4.
				case 4:
					tformat = format_detect(src, req->actual);

					if(DOP_DETECT_ERR_SIZEOVER == tformat)
					{
						pr_debug("[UAC] ERR format, need to check. @Line %d!\n", __LINE__);
						goto skip_copy;
					}
					
					if(DOP_DETECT_PCM == tformat)
					{
						if (open_format == SNDRV_PCM_FORMAT_DSD_U8) {
							remove_marker(src, src, req->actual);
							/* pr_debug("[UAC] size %d, %d!\n", req->actual, req->actual/2); */
							req->actual = req->actual/2;
						}
					}else
					if(DOP_DETECT_DOP == tformat)
					{
						//remove maker and zero. then combine two 16bit to 32 bit.
						if (0 != remain_byte)
						{
							char *pconbine = &combine_buffer[0];
							memcpy(pconbine,remaindata,remain_byte);
							memcpy(pconbine+remain_byte,src,req->actual);
							req->actual = req->actual + remain_byte;
							src = pconbine;
						}
							
						remain_byte = req->actual % 8;
						
						if (0 != remain_byte)
						{
							pr_debug("[UAC] remain_byte:%d  @Line %d!\n", remain_byte, __LINE__);
							req->actual = req->actual - remain_byte;
							memcpy(remaindata,src+(req->actual),remain_byte);
						}

						if (open_format == SNDRV_PCM_FORMAT_DSD_U8) {
							remove_marker(src, src, req->actual);
							/* pr_debug("[UAC] size %d, %d!\n", req->actual, req->actual/2); */
							req->actual = req->actual/2;
						}
					}
					break;
				case 2:
					/* extent to 32 bit. must (EXTENT_BUFF_MAX => (req->actual * 2)) */
					if (EXTENT_BUFF_MAX < (req->actual * 2)) {
						WARN_ON(1);
						dev_err(&uac2->pdev.dev, "%s:%d Error! req->actual(%d) is Big\n",
								 __func__, __LINE__, req->actual);
						dev_err(&uac2->pdev.dev, "Reduce req->actual, fit to EXTENT_BUFF_MAX\n");
						req->actual = EXTENT_BUFF_MAX / 2;
					}

					req->actual = memcpy_pcm32(uac2->pextent, src, req->actual, 16);
					src = uac2->pextent;

					break;
				default:
					pr_debug("[UAC] ERR Bitrate %d @Line %d!\n", tslot, __LINE__);
					break;
			}
			if (((open_format != SNDRV_PCM_FORMAT_DSD_U8) && ((req->actual % 8) != 0))
			 || ((open_format == SNDRV_PCM_FORMAT_DSD_U8) && ((req->actual % 4) != 0))) {
				wd_uac_fail(-EPROTO);
				goto skip_copy;
			}
		}

		//FIXME: not send uevent to user, beacuse maybe effort perfermance

		if (((DSD_ALT == alt) && (gSPEED == USB_SPEED_HIGH)) || (open_format == SNDRV_PCM_FORMAT_DSD_U8))
		{
			//DSD Data, need switch place.
			dsd_deal_data(src, src, req->actual);
		}
		
		if ((prm->dma_bytes - prm->hw_ptr) < req->actual) {
			tmpptr = prm->dma_bytes - prm->hw_ptr;
			if(0)
			{
				pr_debug("[UAC] maybe error 0x%x 0x%x 0x%x @Line %d!\n", prm->dma_bytes, prm->hw_ptr, req->actual, __LINE__);
				for(j = 0; j < req->actual; j ++)
				{
					pr_debug("%x ", src[j]);
				}		
				pr_debug("\n");
			}
		}


        /* Prepare the event associated with this packet for source reporting. */
        if(substream->stream==SNDRV_PCM_STREAM_CAPTURE){
            uac2_uevent_msg_create(uac2,&msgevent,alt,tformat,tbitwidth,tslot);
            event_created=1;
        }
        spin_lock_irqsave(&prm->lock,flags);
        if(!prm->dma_area||!prm->dma_bytes||!prm->period_size||prm->ss!=substream){
            spin_unlock_irqrestore(&prm->lock,flags);update_alsa=false;goto skip_copy;
        }
        if(substream->stream==SNDRV_PCM_STREAM_CAPTURE){
            unsigned frame_bytes=open_format==SNDRV_PCM_FORMAT_DSD_U8?2:8;
            spin_lock(&wd_uac_data_lock);
            boundary_rc=wd_uac_quiescing?-ESTALE:req->actual>prm->dma_bytes?-EPROTO:
                req->actual?sony_usb_boundary_copy(&wd_uac_boundary,0,req->actual/frame_bytes):0;
            if(boundary_rc){
                if(boundary_rc!=-ESTALE){if(!wd_uac_boundary.error)wd_uac_boundary.error=boundary_rc;wd_uac_quiescing=1;}
                spin_unlock(&wd_uac_data_lock);spin_unlock_irqrestore(&prm->lock,flags);
                update_alsa=false;goto skip_copy;
            }
        }
        update_alsa=(prm->hw_ptr%prm->period_size)+req->actual>=prm->period_size;
        dst=substream->stream==SNDRV_PCM_STREAM_CAPTURE?prm->dma_area+prm->hw_ptr:req->buf;
        if(substream->stream==SNDRV_PCM_STREAM_PLAYBACK)src=prm->dma_area+prm->hw_ptr;
        tmpptr=prm->dma_bytes-prm->hw_ptr;
        if(tmpptr<req->actual){memcpy(dst,src,tmpptr);memcpy(prm->dma_area,src+tmpptr,req->actual-tmpptr);}
        else memcpy(dst,src,req->actual);
        prm->hw_ptr=(prm->hw_ptr+req->actual)%prm->dma_bytes;
        if(substream->stream==SNDRV_PCM_STREAM_CAPTURE)spin_unlock(&wd_uac_data_lock);
        spin_unlock_irqrestore(&prm->lock,flags);

#if(DEBUG_DUMPDATA)
		if ((NULL != log_filp) &&
		   (0 != req->actual))
		{
			if (!dump_buf)
			{
				pr_err("[UAC] buffer pointer is NULL @Line %d!\n", __LINE__);
			}

			/* Copy buffer is full, add it to the play_queue */
			if ((dump_buf_size - dump_buf->actual) < req->actual) {
				list_add_tail(&dump_buf->list, &uac2->dump_queue);
				schedule_work(&uac2->dump_work);
				dump_buf = uac2_dump_buffer_alloc(dump_buf_size);
				if (IS_ERR(dump_buf))
				{
					pr_err("[UAC] Malloc Fail @Line %d!\n", __LINE__);
					goto exit;
				}
				//memset(dump_buf->buf, 0, dump_buf_size);
				dump_buf->actual = 0;				
			}
			memcpy(dump_buf->buf + dump_buf->actual, src, req->actual);
		    dump_buf->actual += req->actual;
			uac2->dump_buf = dump_buf;
		}
#endif
	}

skip_copy:
    if(wd_uac_is_quiescing())return;
    if(!event_created)uac2_uevent_msg_create(uac2,&msgevent,alt,tformat,tbitwidth,tslot);
    wd_source_collect_packet(agdev,&msgevent);

requeue_only:

	if (update_alsa)
		snd_pcm_period_elapsed(substream);

	return;
}


#if 0//FEEDBACK_FUNCTION
void
alsa_buffer_status(unsigned char *dst, unsigned bsize)
{
	unsigned char *src;
	
	src = kzalloc(bsize, GFP_KERNEL);
	if (!src)
	{
		pr_err("[UAC] %s:%d Error!\n", __func__, __LINE__);
		goto EXIT;
	}

	//TODO get the buffer status

	memcpy(dst, src, bsize);

EXIT:
	kfree(src);
	return;
}

static void
feedback_complete(struct usb_ep *ep, struct usb_request *req)
{
	int status = req->status;
	struct uac2_feedback *pfeedb = req->context;
	int ret = 0;

	pr_debug("[UAC] feedback Comple called, Frame(0x%x) @Line %d!\n", req->start_frame, __LINE__);

	if (status)
		pr_debug("[UAC] %s: feedback_complete status(%d) %d/%d\n",
			__func__, status, req->actual, req->length);

	req->start_frame = (req->start_frame + 1) % 2048; //maybe need modify. ,frame max num
		
	ret = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (ret)
		pr_err("[UAC] %d Error @Line %d!\n", ret, __LINE__);

	return;
}

#endif

static int
uac2_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
struct snd_uac2_chip *uac2 = snd_pcm_substream_chip(substream);
	struct uac2_rtd_params *prm;
	unsigned long flags;
	int err = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		prm = &uac2->p_prm;
	else
		prm = &uac2->c_prm;

	spin_lock_irqsave(&prm->lock, flags);


    switch(cmd){
    case SNDRV_PCM_TRIGGER_START:
    case SNDRV_PCM_TRIGGER_RESUME:
        if(!prm->dma_area||!prm->dma_bytes||!prm->period_size){err=-EINVAL;break;}
        if(substream->stream==SNDRV_PCM_STREAM_CAPTURE){
            spin_lock(&wd_uac_data_lock);
            err=wd_uac_quiescing?-ESTALE:sony_usb_boundary_capture_start(&wd_uac_boundary,wd_uac_boundary.source_epoch);
            spin_unlock(&wd_uac_data_lock);
            if(err)break;
        }
        prm->hw_ptr=0;prm->ss=substream;break;
    case SNDRV_PCM_TRIGGER_STOP:
    case SNDRV_PCM_TRIGGER_SUSPEND:
        if(substream->stream==SNDRV_PCM_STREAM_CAPTURE){
            spin_lock(&wd_uac_data_lock);
            if((wd_uac_boundary.flags&(SONY_USB_BOUNDARY_BOUND|SONY_USB_BOUNDARY_CAPTURE_LIVE))==(SONY_USB_BOUNDARY_BOUND|SONY_USB_BOUNDARY_CAPTURE_LIVE))
                err=sony_usb_boundary_capture_stop(&wd_uac_boundary,wd_uac_boundary.capture_id);
            spin_unlock(&wd_uac_data_lock);
        }
        prm->hw_ptr=0;prm->ss=NULL;break;
    default:err=-EINVAL;
    }
	pr_debug("[UAC] uac2_pcm_trigger %d\n", cmd);

	spin_unlock_irqrestore(&prm->lock, flags);

	/* Clear buffer after Play stops */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK && !prm->ss)
		memset(prm->rbuf, 0, prm->max_psize * USB_XFERS);

	return err;
}

static snd_pcm_uframes_t uac2_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_uac2_chip *uac2 = snd_pcm_substream_chip(substream);
	struct uac2_rtd_params *prm;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		prm = &uac2->p_prm;
	else
		prm = &uac2->c_prm;

	return bytes_to_frames(substream->runtime, prm->hw_ptr);
}

static int uac2_pcm_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *hw_params)
{
struct snd_uac2_chip *uac2=snd_pcm_substream_chip(substream);
    struct audio_dev *a=uac2_to_agdev(uac2);
    struct uac2_rtd_params *prm=substream->stream==SNDRV_PCM_STREAM_CAPTURE?&uac2->c_prm:&uac2->p_prm;
    unsigned long flags;int rc;
    spin_lock_irqsave(&a->wd_process_lock,flags);spin_lock(&prm->lock);
    prm->ss=NULL;prm->hw_ptr=0;
    if(substream->stream==SNDRV_PCM_STREAM_CAPTURE){
        spin_lock(&wd_uac_data_lock);
        if((wd_uac_boundary.flags&(SONY_USB_BOUNDARY_BOUND|SONY_USB_BOUNDARY_CAPTURE_LIVE))==(SONY_USB_BOUNDARY_BOUND|SONY_USB_BOUNDARY_CAPTURE_LIVE))
            sony_usb_boundary_capture_stop(&wd_uac_boundary,wd_uac_boundary.capture_id);
        spin_unlock(&wd_uac_data_lock);
    }
prm->dma_area=NULL;prm->dma_bytes=0;prm->period_size=0;
spin_unlock(&prm->lock);spin_unlock_irqrestore(&a->wd_process_lock,flags);
    rc=snd_pcm_lib_alloc_vmalloc_buffer(substream,params_buffer_bytes(hw_params));
    if(rc<0)return rc;
    spin_lock_irqsave(&a->wd_process_lock,flags);spin_lock(&prm->lock);
    substream->runtime->dma_bytes=params_buffer_bytes(hw_params);
    prm->dma_area=substream->runtime->dma_area;prm->dma_bytes=substream->runtime->dma_bytes;
    prm->period_size=params_period_bytes(hw_params);
    spin_unlock(&prm->lock);spin_unlock_irqrestore(&a->wd_process_lock,flags);
    return rc;
}

static int uac2_pcm_hw_free(struct snd_pcm_substream *substream)
{
struct snd_uac2_chip *uac2=snd_pcm_substream_chip(substream);
    struct audio_dev *a=uac2_to_agdev(uac2);
    struct uac2_rtd_params *prm=substream->stream==SNDRV_PCM_STREAM_CAPTURE?&uac2->c_prm:&uac2->p_prm;
    unsigned long flags;
    spin_lock_irqsave(&a->wd_process_lock,flags);spin_lock(&prm->lock);
    prm->ss=NULL;prm->hw_ptr=0;
    if(substream->stream==SNDRV_PCM_STREAM_CAPTURE){
        spin_lock(&wd_uac_data_lock);
        if((wd_uac_boundary.flags&(SONY_USB_BOUNDARY_BOUND|SONY_USB_BOUNDARY_CAPTURE_LIVE))==(SONY_USB_BOUNDARY_BOUND|SONY_USB_BOUNDARY_CAPTURE_LIVE))
            sony_usb_boundary_capture_stop(&wd_uac_boundary,wd_uac_boundary.capture_id);
        spin_unlock(&wd_uac_data_lock);
    }
prm->dma_area=NULL;prm->dma_bytes=0;prm->period_size=0;
spin_unlock(&prm->lock);spin_unlock_irqrestore(&a->wd_process_lock,flags);
return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int uac2_pcm_open(struct snd_pcm_substream *substream)
{
struct snd_uac2_chip *uac2 = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_dev *agdev = container_of(uac2, struct audio_dev, uac2);
	short alt = agdev->as_out_alt;

	runtime->hw = uac2_pcm_hardware;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		runtime->hw.rate_min = p_srate;
		runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE; /* ! p_ssize ! */
		runtime->hw.channels_min = num_channels(p_chmask);
		runtime->hw.period_bytes_min = 2 * uac2->p_prm.max_psize
						/ runtime->hw.periods_min;
	} else {
		if (DOP_DETECT_DOP == tformat)
		{
			runtime->hw.rate_min = c_srate/2;
		}else
		{
			runtime->hw.rate_min = c_srate;
		}
		pr_debug("[UAC] %s format=%d, f_allow=%d\n", __func__, tformat, f_allow);
		
		if (((DSD_ALT == alt) && (gSPEED == USB_SPEED_HIGH)) || (DOP_DETECT_DOP == tformat))
		{
			runtime->hw.formats = SNDRV_PCM_FMTBIT_DSD_U8; /* ! c_ssize ! */
		}else
		{
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S32_LE; /* ! c_ssize ! */
		}
		runtime->hw.channels_min = num_channels(c_chmask);
		runtime->hw.period_bytes_min = 2 * QMU_PACKAGE_LENGTH            //maybe modify
						/ runtime->hw.periods_min;
	}

	runtime->hw.rate_max = runtime->hw.rate_min;
	runtime->hw.channels_max = runtime->hw.channels_min;

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	return 0;
}

/* ALSA cries without these function pointers */
static int uac2_pcm_null(struct snd_pcm_substream *substream)
{
	pr_debug("[UAC] %s format=%d, f_allow=%d\n", __func__, tformat, f_allow);
	return 0;
}

static int wd_uac_pcm_close(struct snd_pcm_substream *substream)
{

    struct snd_uac2_chip *uac2=snd_pcm_substream_chip(substream);
    struct audio_dev *a=uac2_to_agdev(uac2);
    struct uac2_rtd_params *prm=substream->stream==SNDRV_PCM_STREAM_CAPTURE?&uac2->c_prm:&uac2->p_prm;
    unsigned long flags;
    spin_lock_irqsave(&a->wd_process_lock,flags);spin_lock(&prm->lock);
    prm->ss=NULL;prm->hw_ptr=0;
    if(substream->stream==SNDRV_PCM_STREAM_CAPTURE){
        spin_lock(&wd_uac_data_lock);
        if((wd_uac_boundary.flags&(SONY_USB_BOUNDARY_BOUND|SONY_USB_BOUNDARY_CAPTURE_LIVE))==(SONY_USB_BOUNDARY_BOUND|SONY_USB_BOUNDARY_CAPTURE_LIVE))
            sony_usb_boundary_capture_stop(&wd_uac_boundary,wd_uac_boundary.capture_id);
        spin_unlock(&wd_uac_data_lock);
    }
spin_unlock(&prm->lock);spin_unlock_irqrestore(&a->wd_process_lock,flags);
return 0;
}
static struct snd_pcm_ops uac2_pcm_ops = {
	.open = uac2_pcm_open,
	.close = wd_uac_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = uac2_pcm_hw_params,
	.hw_free = uac2_pcm_hw_free,
	.trigger = uac2_pcm_trigger,
	.pointer = uac2_pcm_pointer,
	.prepare = uac2_pcm_null,
	.page = snd_pcm_lib_get_vmalloc_page,
};

static int snd_uac2_probe(struct platform_device *pdev)
{
	struct snd_uac2_chip *uac2 = pdev_to_uac2(pdev);
	struct snd_card *card;
	struct snd_pcm *pcm;
	int err;

	/* Choose any slot, with no id */
	err = snd_card_create(4, NULL, THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	uac2->card = card;

	/*
	 * Create first PCM device
	 * Create a substream only for non-zero channel streams
	 */
	err = snd_pcm_new(uac2->card, "UAC2 PCM", 0,
			       p_chmask ? 1 : 0, c_chmask ? 1 : 0, &pcm);
	if (err < 0)
		goto snd_fail;

	strcpy(pcm->name, "UAC2 PCM");
	pcm->private_data = uac2;

	uac2->pcm = pcm;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &uac2_pcm_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &uac2_pcm_ops);

	strcpy(card->driver, "UAC2_Gadget");
	strcpy(card->shortname, "UAC2_Gadget");
	sprintf(card->longname, "UAC2_Gadget %i", pdev->id);

	snd_card_set_dev(card, &pdev->dev);

	/* CPU-only ALSA rings use vmalloc at hw_params. USB request DMA
	 * buffers stay physically backed and keep the original lifecycle. */

	err = snd_card_register(card);
	pr_debug("[UAC] %s . %s . %s\n", pcm->name, pcm->streams[0].substream->name, pcm->streams[1].substream->name);
	if (!err) {
		platform_set_drvdata(pdev, card);
		return 0;
	}

snd_fail:
	snd_card_free(card);

	uac2->pcm = NULL;
	uac2->card = NULL;

	return err;
}

static int snd_uac2_remove(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);

	if (card)
		return snd_card_free(card);

	return 0;
}

static void snd_uac2_release(struct device *dev)
{
dev_dbg(dev, "releasing '%s'\n", dev_name(dev));

    struct platform_device *pdev=container_of(dev,struct platform_device,dev);
    struct snd_uac2_chip *uac2=pdev_to_uac2(pdev);
    complete(&uac2_to_agdev(uac2)->wd_platform_released);
}

static int alsa_uac2_init(struct audio_dev *agdev)
{
struct snd_uac2_chip *uac2 = &agdev->uac2;
	int err;

	uac2->pdrv.probe = snd_uac2_probe;
	uac2->pdrv.remove = snd_uac2_remove;
	uac2->pdrv.driver.name = uac2_name;

	uac2->pdev.id = 0;
	uac2->pdev.name = uac2_name;
	uac2->pdev.dev.release = snd_uac2_release;

	/* Register snd_uac2 driver */
	err = platform_driver_register(&uac2->pdrv);
	if (err)
		return err;

	/* Register snd_uac2 device */
	err = platform_device_register(&uac2->pdev);
    agdev->wd_pdev_initialized=1;
	if(err){platform_driver_unregister(&uac2->pdrv);platform_device_put(&uac2->pdev);}

	return err;
}

static void alsa_uac2_exit(struct audio_dev *agdev)
{
	struct snd_uac2_chip *uac2 = &agdev->uac2;

	platform_driver_unregister(&uac2->pdrv);
	platform_device_unregister(&uac2->pdev);
}


/* --------- USB Function Interface ------------- */

enum {
	STR_ASSOC,
	STR_IF_CTRL,
	STR_CLKSRC_IN,
	STR_CLKSRC_OUT,
	STR_USB_IT,
	STR_IO_IT,
	STR_USB_OT,
	STR_IO_OT,
	STR_AS_OUT_ALT0,
	STR_AS_OUT_ALT1,
	STR_AS_IN_ALT0,
	STR_AS_IN_ALT1,
};

static char clksrc_in[8];
static char clksrc_out[8];
extern char product_string[256];

static struct usb_string strings_fn[] = {
	[STR_ASSOC].s = &product_string[0],
	[STR_IF_CTRL].s = "Topology Control",
	[STR_CLKSRC_IN].s = clksrc_in,
	[STR_CLKSRC_OUT].s = clksrc_out,
	[STR_USB_IT].s = "USBH Out",
	[STR_IO_IT].s = "USBD Out",
	[STR_USB_OT].s = "USBH In",
	[STR_IO_OT].s = "USBD In",
	[STR_AS_OUT_ALT0].s = &product_string[0],
	[STR_AS_OUT_ALT1].s = &product_string[0],
	[STR_AS_IN_ALT0].s = "Capture Inactive",
	[STR_AS_IN_ALT1].s = "Capture Active",
	{ },
};

static struct usb_gadget_strings str_fn = {
	.language = 0x0409,	/* en-us */
	.strings = strings_fn,
};

static struct usb_gadget_strings *fn_strings[] = {
	&str_fn,
	NULL,
};

static struct usb_interface_assoc_descriptor iad_desc = {
	.bLength = sizeof iad_desc,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,

	.bFirstInterface = 0,
	.bInterfaceCount = 2,
	.bFunctionClass = USB_CLASS_AUDIO,
	.bFunctionSubClass = UAC2_FUNCTION_SUBCLASS_UNDEFINED,
	.bFunctionProtocol = UAC_VERSION_2,
};

/* Audio Control Interface */
static struct usb_interface_descriptor std_ac_if_desc = {
	.bLength = sizeof std_ac_if_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOCONTROL,
	.bInterfaceProtocol = UAC_VERSION_2,
};

/* Clock source for OUT traffic */
struct uac_clock_source_descriptor out_clk_src_desc = {
	.bLength = sizeof out_clk_src_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC2_CLOCK_SOURCE,
	.bClockID = USB_OUT_CLK_ID,
	.bmAttributes = UAC_CLOCK_SOURCE_TYPE_INT_PROG,
	.bmControls = 7,
	.bAssocTerminal = 0,
};

#if 1
/* Clock Selector for OUT traffic */
struct uac_clock_selector_descriptor out_clk_sel_desc = {
	.bLength = sizeof out_clk_sel_desc,	
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC2_CLOCK_SELECTOR,
	.bClockID = USB_OUT_CLK_SEL_ID,
	.bNrInPins = 0x01,
	.baCSourceID[0]=USB_OUT_CLK_ID,
	.baCSourceID[1]=0x3,
};
#endif

/* Input Terminal for USB_OUT */
struct uac2_input_terminal_descriptor usb_out_it_desc = {
	.bLength = sizeof usb_out_it_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_INPUT_TERMINAL,
	.bTerminalID = USB_OUT_IT_ID,
	.wTerminalType = cpu_to_le16(UAC_TERMINAL_STREAMING),
	.bAssocTerminal = 0,
	.bCSourceID = USB_OUT_CLK_SEL_ID,
	.iChannelNames = 0,
	.bmControls = (CONTROL_RDWR << COPY_CTRL),
};

/* Output Terminal for USB_IN */
struct uac2_output_terminal_descriptor usb_in_ot_desc = {
	.bLength = sizeof usb_in_ot_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_OUTPUT_TERMINAL,
	.bTerminalID = USB_IN_OT_ID,
	.wTerminalType = cpu_to_le16(UAC_OUTPUT_TERMINAL_SPEAKER),
	.bAssocTerminal = 0,
	.bSourceID = USB_FEATURE_UNIT_ID,
	.bCSourceID = USB_OUT_CLK_ID,
	.bmControls = (CONTROL_RDWR << COPY_CTRL),
};

struct uac2_feature_unit_descriptor funit_desc = {
	.bLength = sizeof funit_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FEATURE_UNIT,
	.bUnitID = USB_FEATURE_UNIT_ID,
	.bSourceID = USB_OUT_IT_ID,
	//	.bmaControls[]=	
	{0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,
	 0x00}, /* variable length */
};


struct uac2_ac_header_descriptor ac_hdr_desc = {
	.bLength = sizeof ac_hdr_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_MS_HEADER,
	.bcdADC = cpu_to_le16(0x200),
	.bCategory = UAC2_FUNCTION_IO_BOX,
//TODO: maybe need modify
	.wTotalLength = sizeof ac_hdr_desc + sizeof out_clk_src_desc + sizeof funit_desc + sizeof out_clk_sel_desc
			 + sizeof usb_out_it_desc + sizeof usb_in_ot_desc,
	.bmControls = 0,
};

/* Audio Streaming OUT Interface - Alt0 */
static struct usb_interface_descriptor std_as_out_if0_desc = {
	.bLength = sizeof std_as_out_if0_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

/* Audio Streaming OUT Interface - Alt1 */
static struct usb_interface_descriptor std_as_out_if1_desc = {
	.bLength = sizeof std_as_out_if1_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 1,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

static struct usb_interface_descriptor std_as_out_if2_desc = {
	.bLength = sizeof std_as_out_if2_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 2,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

static struct usb_interface_descriptor std_as_out_if3_desc = {
	.bLength = sizeof std_as_out_if3_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 3,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

static struct usb_interface_descriptor std_as_out_if4_desc = {
	.bLength = sizeof std_as_out_if4_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 4,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

static struct usb_interface_descriptor std_as_out_if5_desc = {
	.bLength = sizeof std_as_out_if5_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 5,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

static struct usb_interface_descriptor std_as_out_if6_desc = {
	.bLength = sizeof std_as_out_if6_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 6,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

static struct usb_interface_descriptor std_as_out_if7_desc = {
	.bLength = sizeof std_as_out_if7_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 7,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};


/* Audio Stream OUT Intface Desc */
struct uac2_as_header_descriptor as_out_hdr_desc = {
	.bLength = sizeof as_out_hdr_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_AS_GENERAL,
	.bTerminalLink = USB_OUT_IT_ID,
	.bmControls = 0,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bmFormats = cpu_to_le32(UAC_FORMAT_TYPE_I_PCM),
	.iChannelNames = 0,
};

//for raw data
struct uac2_as_header_descriptor as_out_raw_hdr_desc = {
	.bLength = sizeof as_out_raw_hdr_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_AS_GENERAL,
	.bTerminalLink = USB_OUT_IT_ID,
	.bmControls = 0,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bmFormats = 0x80000000,//cpu_to_le32(UAC_FORMAT_TYPE_I_PCM),
	.iChannelNames = 0,
};

/* Audio USB_OUT Format */
struct uac2_format_type_i_descriptor as_out_fmt1_desc = {
	.bLength = sizeof as_out_fmt1_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FORMAT_TYPE,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bSubslotSize = 4,
	.bBitResolution = 32,
};

struct uac2_format_type_i_descriptor as_out_fmt2_desc = {
	.bLength = sizeof as_out_fmt2_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FORMAT_TYPE,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bSubslotSize = 4,
	.bBitResolution = 24,
};

struct uac2_format_type_i_descriptor as_out_fmt3_desc = {
	.bLength = sizeof as_out_fmt3_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FORMAT_TYPE,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bSubslotSize = 4,
	.bBitResolution = 16,
};

struct uac2_format_type_i_descriptor as_out_fmt4_desc = {
	.bLength = sizeof as_out_fmt4_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FORMAT_TYPE,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bSubslotSize = 3,
	.bBitResolution = 24,
};

struct uac2_format_type_i_descriptor as_out_fmt5_desc = {
	.bLength = sizeof as_out_fmt5_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FORMAT_TYPE,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bSubslotSize = 3,
	.bBitResolution = 16,
};

struct uac2_format_type_i_descriptor as_out_fmt6_desc = {
	.bLength = sizeof as_out_fmt6_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FORMAT_TYPE,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bSubslotSize = 2,
	.bBitResolution = 16,
};

//raw data
struct uac2_format_type_i_descriptor as_out_fmt7_desc = {
	.bLength = sizeof as_out_fmt7_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FORMAT_TYPE,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bSubslotSize = 4,
	.bBitResolution = 32,
};


/* STD AS ISO OUT Endpoint */
struct usb_endpoint_descriptor fs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	//.wMaxPacketSize = 0x0188,
	.bInterval = 1,
};

struct usb_endpoint_descriptor hs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	//.wMaxPacketSize = 0x0188,
	.bInterval = 1,
};


/* CS AS ISO OUT Endpoint */
static struct uac2_iso_endpoint_descriptor as_iso_out_desc = {
	.bLength = sizeof as_iso_out_desc,
	.bDescriptorType = USB_DT_CS_ENDPOINT,

	.bDescriptorSubtype = UAC_EP_GENERAL,
	.bmAttributes = 0,
	.bmControls = 0,
	.bLockDelayUnits = 0x02,
	.wLockDelay = 0x08,
};


/* STD AS ISO IN Endpoint */
struct usb_endpoint_descriptor fs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_USAGE_FEEDBACK,
//	.wMaxPacketSize = 4,
	.bInterval = 1,
};

struct usb_endpoint_descriptor hs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_USAGE_FEEDBACK,
//	.wMaxPacketSize = 4,
	.bInterval = 4,
};


static struct usb_descriptor_header *fs_audio_desc[] = {
	(struct usb_descriptor_header *)&iad_desc,
	(struct usb_descriptor_header *)&std_ac_if_desc,

	(struct usb_descriptor_header *)&ac_hdr_desc,
	(struct usb_descriptor_header *)&out_clk_src_desc,
	(struct usb_descriptor_header *)&out_clk_sel_desc,
	(struct usb_descriptor_header *)&usb_out_it_desc,
    (struct usb_descriptor_header *)&funit_desc	,
	(struct usb_descriptor_header *)&usb_in_ot_desc,
		
	(struct usb_descriptor_header *)&std_as_out_if0_desc,
	
	(struct usb_descriptor_header *)&std_as_out_if1_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt1_desc,
	(struct usb_descriptor_header *)&fs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&fs_epin_desc,

	(struct usb_descriptor_header *)&std_as_out_if2_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt2_desc,
	(struct usb_descriptor_header *)&fs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&fs_epin_desc,

	(struct usb_descriptor_header *)&std_as_out_if3_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt3_desc,
	(struct usb_descriptor_header *)&fs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&fs_epin_desc,

	(struct usb_descriptor_header *)&std_as_out_if4_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt4_desc,
	(struct usb_descriptor_header *)&fs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&fs_epin_desc,	

	(struct usb_descriptor_header *)&std_as_out_if5_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt5_desc,
	(struct usb_descriptor_header *)&fs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&fs_epin_desc,	

	(struct usb_descriptor_header *)&std_as_out_if6_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt6_desc,
	(struct usb_descriptor_header *)&fs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&fs_epin_desc,		

	(struct usb_descriptor_header *)&std_as_out_if7_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt7_desc,
	(struct usb_descriptor_header *)&fs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&fs_epin_desc,		

	NULL,
};

static struct usb_descriptor_header *hs_audio_desc[] = {
	(struct usb_descriptor_header *)&iad_desc,
	(struct usb_descriptor_header *)&std_ac_if_desc,

	(struct usb_descriptor_header *)&ac_hdr_desc,
	(struct usb_descriptor_header *)&out_clk_src_desc,
	(struct usb_descriptor_header *)&out_clk_sel_desc,
	(struct usb_descriptor_header *)&usb_out_it_desc,
	(struct usb_descriptor_header *)&funit_desc	,
	(struct usb_descriptor_header *)&usb_in_ot_desc,

	(struct usb_descriptor_header *)&std_as_out_if0_desc,
	
	(struct usb_descriptor_header *)&std_as_out_if1_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt1_desc,
	(struct usb_descriptor_header *)&hs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&hs_epin_desc,

	(struct usb_descriptor_header *)&std_as_out_if2_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt2_desc,
	(struct usb_descriptor_header *)&hs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&hs_epin_desc,
	
	(struct usb_descriptor_header *)&std_as_out_if3_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt3_desc,
	(struct usb_descriptor_header *)&hs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&hs_epin_desc,
	
	(struct usb_descriptor_header *)&std_as_out_if4_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt4_desc,
	(struct usb_descriptor_header *)&hs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&hs_epin_desc,	
		
	(struct usb_descriptor_header *)&std_as_out_if5_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt5_desc,
	(struct usb_descriptor_header *)&hs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&hs_epin_desc,	
	
	(struct usb_descriptor_header *)&std_as_out_if6_desc,
	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt6_desc,
	(struct usb_descriptor_header *)&hs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&hs_epin_desc,		

	(struct usb_descriptor_header *)&std_as_out_if7_desc,
	(struct usb_descriptor_header *)&as_out_raw_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt7_desc,
	(struct usb_descriptor_header *)&hs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,
	(struct usb_descriptor_header *)&hs_epin_desc,		

	NULL,
};

struct cntrl_cur_lay3 {
	__u32	dCUR;
};

struct cntrl_range_lay3 {
	__u16	wNumSubRanges;
	__u32	dMIN;
	__u32	dMAX;
	__u32	dRES;
} __packed;

static inline void
free_ep(struct uac2_rtd_params *prm, struct usb_ep *ep)
{
wd_retire_requests(prm,ep);
}

#if 0//FEEDBACK_FUNCTION
static inline void
feedback_free_ep(struct usb_ep *ep)
{
	if (FeedBackReq) {
		usb_ep_dequeue(ep, FeedBackReq);
		usb_ep_free_request(ep, FeedBackReq);
		FeedBackReq = NULL;
	}

	if (usb_ep_disable(ep))
		pr_err("[UAC] usb_ep_disable fail @Line %d\n", __LINE__);
}
#endif

static void uac2_get_bufferstatus(u32 *status)
{
struct audio_dev *a=wd_parent_current(0);
    struct snd_uac2_chip *uac2;
	struct uac2_rtd_params *prm;unsigned long flags;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	snd_pcm_sframes_t remaindata=0;
	unsigned int OriginalClock = 0;
	unsigned int CurrentClock = 0;

	*status=0;
    if(!a)return;
    if(!spin_trylock_irqsave(&a->wd_process_lock,flags)){wd_parent_put(a);return;}
    uac2=&a->uac2;prm=&uac2->c_prm;
    if(a->wd_closing||wd_uac_is_quiescing())goto exit;
	if (!prm->ep_enabled)
	{
		goto exit;
	}
	
	if (gFirstCount > 0) {
		gFirstCount--;
		/* pr_debug("gFirstCount feedback=0x%04X\n", CurrentClock); */
		goto exit;
	}
	

	substream = prm->ss;

	if (gSPEED == USB_SPEED_HIGH)
		switch (c_srate)
		{
			case  32000:
			case  48000:
			case  96000:
			case 192000:
			case 384000:
				OriginalClock = (c_srate / 1000) << 13;
				break;
			case  44100:
				OriginalClock = 361267;
				break;
			case  88200:
				OriginalClock = 722534;
				break;
			case 176400:
				OriginalClock = 1445068;
				break;
			case 352800:
				OriginalClock = 2890137;
				break;
			default:
				OriginalClock = (c_srate << 13) / 1000;
		}
	else
		switch (c_srate)
		{
			case  32000:
			case  48000:
			case  96000:
			case 192000:
			case 384000:
				OriginalClock = (c_srate / 1000) << 14;
				break;
			case  44100:
				OriginalClock = 722534;
				break;
			case  88200:
				OriginalClock = 1445068;
				break;
			case 176400:
				OriginalClock = 2890137;
				break;
			case 352800:
				OriginalClock = 5780275;
				break;
			default:
				OriginalClock = (c_srate << 14) / 1000;
		}

	CurrentClock = OriginalClock;
    if(wd_source_feedback_clock(a->wd_source,c_srate,OriginalClock,f_thresh,f_plus,f_minus,&CurrentClock))goto exit;

	/* Do nothing if ALSA isn't active */
	if (!substream)
	{
		goto exit;
	}

	runtime = substream->runtime;

	if(snd_pcm_kernel_ioctl(substream,SNDRV_PCM_IOCTL_DELAY,&remaindata))goto exit;
	
	if (remaindata < (runtime->buffer_size * f_thresh / 100))
	{
		CurrentClock = OriginalClock + (OriginalClock * f_plus) / 10000;
		if (CurrentClock >= (OriginalClock + 0xFFFF))
		{
			CurrentClock = OriginalClock + 0xFFFF;
		}
	}
	else
	{
		CurrentClock = OriginalClock - (OriginalClock * f_minus) / 10000;
		if (CurrentClock <= (OriginalClock - 0xFFFF))
		{
			CurrentClock = OriginalClock - 0xFFFF;
		}
	}

#if DISP_USAGE  // set 1 is display log
	{
		char s_usage[8];
		char s_clock[8];
		int u_usage,l_usage;
		int u_clock,l_clock;
		
		u_usage = (10000 * remaindata) / (runtime->buffer_size) / 100;
		l_usage = (10000 * remaindata) / (runtime->buffer_size) % 100;

		u_clock = 100 * CurrentClock / OriginalClock ;
		l_clock = (100 * CurrentClock) /(OriginalClock / 100)  % 100;
		
		sprintf(s_usage, "%3d.%02d%%",u_usage,l_usage);
		sprintf(s_clock, "%3d.%02d%%",u_clock,l_clock);
		
//		if ((10 > u_usage) ||  (90 <  u_usage)) //filter log for display Over and Underrun.
		pr_debug("delay: %4d/%d frames(Usage: %s, Clock =%s\n", remaindata, runtime->buffer_size, s_usage, s_clock);
	}
#endif

exit:

	{
		char feedback[4] = {0,0,0,0};
		
		feedback[3] = (CurrentClock & 0xFF000000) >> 24;
		feedback[2] = (CurrentClock & 0x00FF0000) >> 16;
		feedback[1] = (CurrentClock & 0x0000FF00) >> 8;;
		feedback[0] = (CurrentClock & 0x000000FF);
		
		memcpy(status, feedback, 4);
	}
	
	spin_unlock_irqrestore(&a->wd_process_lock,flags);wd_parent_put(a);
	return;
}

/* Shared cleanup for a failed bind and normal unbind after ALSA teardown. */
static void afunc_partial_cleanup(struct usb_function *fn)
{
struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	reg_uac2_buffer_status(NULL);
	if(uac2->sock)uac2_netlink_exit(uac2->sock);
	uac2->sock = NULL;
	vfree(uac2->pextent);
	uac2->pextent = NULL;
	kfree(uac2->p_prm.rbuf);
	uac2->p_prm.rbuf = NULL;
	kfree(uac2->c_prm.rbuf);
	uac2->c_prm.rbuf = NULL;
	{ unsigned wd_i;for(wd_i=0;wd_i<USB_XFERS;++wd_i){
		kfree(agdev->wd_capture_bufs[wd_i]);agdev->wd_capture_bufs[wd_i]=NULL;
	} }
	usb_free_all_descriptors(fn);
	if (agdev->in_ep && agdev->in_ep->driver_data==agdev)
		agdev->in_ep->driver_data = NULL;
	if (agdev->out_ep && agdev->out_ep->driver_data==agdev)
		agdev->out_ep->driver_data = NULL;
}
static void wd_audio_cleanup_work(struct work_struct *work)
{
    struct audio_dev *a=container_of(work,struct audio_dev,wd_cleanup_work);
    unsigned long flags;
    struct wd_uac_source *source;
    spin_lock_irqsave(&wd_uac_data_lock,flags);source=a->wd_source;a->wd_source=NULL;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);wd_source_destroy(source);
    if(a->wd_alsa_initialized){alsa_uac2_exit(a);a->wd_alsa_initialized=0;}
    /* Unregister only drops the device's initial reference. Its embedded
     * storage cannot be freed until device-core users reach the release hook. */
    if(a->wd_pdev_initialized)wait_for_completion(&a->wd_platform_released);
    afunc_partial_cleanup(&a->func);
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    if(wd_parent_retiring)--wd_parent_retiring;
    else if(!wd_uac_boundary.error)wd_uac_boundary.error=-EUCLEAN;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    kfree(a);
}


static int
afunc_bind(struct usb_configuration *cfg, struct usb_function *fn)
{
struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	struct usb_composite_dev *cdev = cfg->cdev;
	struct usb_gadget *gadget = cdev->gadget;
	struct uac2_rtd_params *prm;
//	struct uac2_feedback *pfeedb;
	
	int ret;
	unsigned wd_i;

	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}
	std_ac_if_desc.bInterfaceNumber = ret;
	agdev->ac_intf = ret;
	agdev->ac_alt = 0;

	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}
	std_as_out_if0_desc.bInterfaceNumber = ret;
	std_as_out_if1_desc.bInterfaceNumber = ret;
	std_as_out_if2_desc.bInterfaceNumber = ret;
	std_as_out_if3_desc.bInterfaceNumber = ret;
	std_as_out_if4_desc.bInterfaceNumber = ret;
	std_as_out_if5_desc.bInterfaceNumber = ret;
	std_as_out_if6_desc.bInterfaceNumber = ret;
	std_as_out_if7_desc.bInterfaceNumber = ret;
	agdev->as_out_intf = ret;
	agdev->as_out_alt = 0;

#if(DEBUG_DUMPDATA)
	if(NULL == log_filp)
	{
		log_fs = get_fs();
		set_fs(KERNEL_DS);
        log_filp = filp_open("/mnt/sda1/back",O_RDWR | O_CREAT | O_TRUNC | O_APPEND, 0);
        if(IS_ERR(log_filp))
        {
            pr_err("[UAC] fileopen sda1 error\n");
            log_filp = filp_open("/data/back",O_RDWR | O_CREAT | O_TRUNC | O_APPEND, 0);
            if(IS_ERR(log_filp))
            {
                pr_err("[UAC] fileopen data error\n");
            }
        }
		set_fs(log_fs);

		uac2->dump_buf = uac2_dump_buffer_alloc(dump_buf_size);
		if (IS_ERR(uac2->dump_buf))
			return -ENOMEM;
		//memset(uac2->dump_buf->buf, 0, dump_buf_size);
		uac2->dump_buf->actual = 0;
		spin_lock_init(&uac2->lock);
		INIT_LIST_HEAD(&uac2->dump_queue);
		INIT_WORK(&uac2->dump_work, uac2_dump_data_work);
	}
#endif

#if 0	
	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}
	std_as_in_if0_desc.bInterfaceNumber = ret;
	std_as_in_if1_desc.bInterfaceNumber = ret;
	agdev->as_in_intf = ret;
	agdev->as_in_alt = 0;
#endif	

	uac2->sock = uac2_netlink_init();
	if (!uac2->sock)
		goto err;
	uac2_init_msg(uac2);

	reg_uac2_buffer_status((void *)uac2_get_bufferstatus);

	agdev->out_ep = usb_ep_autoconfig(gadget, &fs_epout_desc);
	if (!agdev->out_ep) {
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}
	pr_debug("[UAC] endpoint name %s\n", agdev->out_ep->name);
	
	agdev->out_ep->driver_data = agdev;

	agdev->in_ep = usb_ep_autoconfig(gadget, &fs_epin_desc);
	if (!agdev->in_ep) {
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}
	agdev->in_ep->driver_data = agdev;

	hs_epout_desc.bEndpointAddress = fs_epout_desc.bEndpointAddress;
	hs_epout_desc.wMaxPacketSize = fs_epout_desc.wMaxPacketSize = 1024;
	if (fs_epout_desc.wMaxPacketSize > 1023) fs_epout_desc.wMaxPacketSize = 1023;
	hs_epin_desc.bEndpointAddress = fs_epin_desc.bEndpointAddress;
	hs_epin_desc.wMaxPacketSize = 0x04;
	fs_epin_desc.wMaxPacketSize = 0x08;

	ret = usb_assign_descriptors(fn, fs_audio_desc, hs_audio_desc, NULL);
	if (ret)
		goto err;

	prm = &agdev->uac2.c_prm;
#if QMU_PACKAGE_NUM
	prm->max_psize = USB_XFERS * QMU_PACKAGE_LENGTH * QMU_PACKAGE_NUM;
	pr_debug("[UAC] prm->max_psize %d\n", prm->max_psize);
	/* QMU maps one 16KiB payload per request. Sharing one64KiB allocation
	 * needlessly requires order-4 pages on this long-lived Android system. */
	for(wd_i=0;wd_i<USB_XFERS;++wd_i){
		agdev->wd_capture_bufs[wd_i]=kzalloc(QMU_PACKAGE_LENGTH*QMU_PACKAGE_NUM,GFP_KERNEL);
		if(!agdev->wd_capture_bufs[wd_i])goto err;
	}
	prm->rbuf=NULL;
#else
	prm->max_psize = hs_epout_desc.wMaxPacketSize;
	prm->rbuf = kzalloc(prm->max_psize * USB_XFERS, GFP_KERNEL);
#endif
#if(DEBUG_DUMPDATA)
	dump_buf_size = prm->max_psize * USB_XFERS;
	pr_debug("[UAC] alloc buffer size 0x%x\n", dump_buf_size);
#endif
	if (!QMU_PACKAGE_NUM && !prm->rbuf) {
		prm->max_psize = 0;
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}

	prm = &agdev->uac2.p_prm;
	prm->max_psize = hs_epin_desc.wMaxPacketSize;
	prm->rbuf = kzalloc(prm->max_psize * USB_XFERS, GFP_KERNEL);
	if (!prm->rbuf) {
		prm->max_psize = 0;
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}
	
	uac2->pextent = vzalloc(EXTENT_BUFF_MAX);
	if (!uac2->pextent) {
		prm->max_psize = 0;
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}

/*
	pfeedb = &agdev->uac2.feedback;
	pfeedb->fb_size = FEEDBACK_BUF_SIZE;
	pfeedb->pbuf= kzalloc(pfeedb->fb_size, GFP_KERNEL);
	if (!pfeedb->pbuf) {
		pfeedb->fb_size = 0;
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}
*/
	ret = alsa_uac2_init(agdev);
	if (ret)
		goto err;
	agdev->wd_alsa_initialized=1;
	ret = wd_uac_bind_boundary();
	if (ret)
		goto err;
    ret=wd_source_create(agdev);if(ret)goto err;
	return 0;
err:
	reg_uac2_buffer_status(NULL);
	return -EINVAL;
}

static void
afunc_unbind(struct usb_configuration *cfg, struct usb_function *fn)
{
struct audio_dev *a=func_to_agdev(fn);unsigned long flags;
    if(!wd_parent_get(a,1))return;
    spin_lock_irqsave(&a->wd_process_lock,flags);
    spin_lock(&wd_uac_data_lock);a->wd_closing=1;spin_unlock(&wd_uac_data_lock);
    wd_source_error(a->wd_source,-ENODEV);
    wd_uac_rate_cancel(a,-ENODEV);wd_uac_quiesce(1);wd_uac_unbind_boundary();
    reg_uac2_buffer_status(NULL);
    if(a->out_ep)free_ep(&a->uac2.c_prm,a->out_ep);
    if(a->in_ep)usb_ep_disable(a->in_ep);
    if(a->out_ep&&a->out_ep->driver_data==a)a->out_ep->driver_data=NULL;
    if(a->in_ep&&a->in_ep->driver_data==a)a->in_ep->driver_data=NULL;
    a->as_out_alt=0;a->as_in_alt=0;
    spin_unlock_irqrestore(&a->wd_process_lock,flags);
    /* The composite core invokes this both on normal removal and when a
     * later function fails to bind. Retire the published parent here so both
     * paths release it exactly once. */
    wd_parent_detach(a);wd_parent_put(a);
}

static int
wd_afunc_set_alt_locked(struct usb_function *fn, unsigned intf, unsigned alt)
{
struct audio_dev *a=func_to_agdev(fn);
    struct snd_uac2_chip *u=&a->uac2;
    struct uac2_rtd_params *prm;struct usb_ep *ep;struct wd_uac_req_ctx *c;
    struct usb_gadget *gadget=fn->config->cdev->gadget;
    struct usb_request *req;unsigned i,j;int rc;
    struct uac2_uevent stop={ACTION_STOP,FORMAT_NONE,BIT_NONE,FREQ_NONE};
    if(alt>MAX_ALT_NUM)return -EINVAL;
    if(intf==a->ac_intf)return alt?-EINVAL:0;
    if(intf!=a->as_out_intf)return -EINVAL; /* native gadget only advertises OUT */
    prm=&u->c_prm;ep=a->out_ep;
    if(alt&&alt==a->as_out_alt&&prm->ep_enabled)return 0;
    if(!alt||alt!=a->as_out_alt)wd_uac_rate_cancel(a,0);
    wd_uac_quiesce(1);
    rc=0;
    if(!alt||a->as_out_alt||prm->ep_enabled)
        rc=wd_uac_publish(u,&stop,alt?SONY_USB_BOUNDARY_CHANGE:SONY_USB_BOUNDARY_NORMAL);
    free_ep(prm,ep);
#if FEEDBACK_FUNCTION
    usb_ep_disable(a->in_ep);
#endif
    a->as_out_alt=0;
    if(!alt)return rc;
    if(rc)return rc;
    rc=wd_parent_can_enable(a);if(rc)return rc;
    tformat=last_tformat;last_tformat=DOP_DETECT_PCM;remain_byte=0;gFirstCount=f_start;
#if FEEDBACK_FUNCTION
    rc=config_ep_by_speed(gadget,fn,a->in_ep);if(rc)goto fail;
    gSPEED=gadget->speed;
#endif
    rc=config_ep_by_speed(gadget,fn,ep);if(rc)goto fail;
#if FEEDBACK_FUNCTION
    rc=usb_ep_enable(a->in_ep);if(rc)goto fail;
#endif
    rc=usb_ep_enable(ep);if(rc)goto fail;
    prm->ep_enabled=true;a->as_out_alt=alt;
    for(i=0;i<USB_XFERS;++i){
        c=wd_request_alloc(a,prm,ep);if(IS_ERR(c)){rc=PTR_ERR(c);goto fail;}
        req=c->ur.req;
#if QMU_PACKAGE_NUM
        req->length=QMU_PACKAGE_NUM*QMU_PACKAGE_LENGTH;
        req->buf=a->wd_capture_bufs[i];req->number_of_packets=QMU_PACKAGE_NUM;
        req->iso_frame_desc=(void *)(a+1)+i*QMU_PACKAGE_NUM*sizeof(struct usb_iso_packet_descriptor);
        for(j=0;j<QMU_PACKAGE_NUM;++j){
            req->iso_frame_desc[j].offset=j*QMU_PACKAGE_LENGTH;
            req->iso_frame_desc[j].length=QMU_PACKAGE_LENGTH;
        }
#else
        req->length=prm->max_psize;req->buf=prm->rbuf+i*req->length;
#endif
        rc=wd_request_install(c,i);
        if(!rc)rc=wd_request_queue(c,1);
        /* A synchronous initial completion cannot be processed under this
         * activation lock. Refuse an empty endpoint instead of claiming PLAY. */
        if(!rc){unsigned long cf;spin_lock_irqsave(&c->lock,cf);
            if(!c->queued)rc=-EIO;spin_unlock_irqrestore(&c->lock,cf);}
        wd_request_put(c); /* creator, separate from slot and queue */
        if(rc)goto fail;
    }
    wd_uac_quiesce(0);return 0;
fail:
    if(rc>=0)rc=-EIO;wd_uac_fail(rc);free_ep(prm,ep);
#if FEEDBACK_FUNCTION
    usb_ep_disable(a->in_ep);
#endif
    a->as_out_alt=0;return rc;
}
static int afunc_set_alt(struct usb_function *fn,unsigned intf,unsigned alt)
{
struct audio_dev *a=func_to_agdev(fn);unsigned long flags;int rc;
    if(!wd_parent_get(a,0))return -ENODEV;
    spin_lock_irqsave(&a->wd_process_lock,flags);rc=wd_afunc_set_alt_locked(fn,intf,alt);
    spin_unlock_irqrestore(&a->wd_process_lock,flags);wd_parent_put(a);return rc;
}


static int
afunc_get_alt(struct usb_function *fn, unsigned intf)
{
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;

	if (intf == agdev->ac_intf)
		return agdev->ac_alt;
	else if (intf == agdev->as_out_intf)
		return agdev->as_out_alt;
	else if (intf == agdev->as_in_intf)
		return agdev->as_in_alt;
	else
		dev_err(&uac2->pdev.dev,
			"%s:%d Invalid Interface %d!\n",
			__func__, __LINE__, intf);

	return -EINVAL;
}

static void
afunc_disable(struct usb_function *fn)
{
struct audio_dev *agdev=func_to_agdev(fn);unsigned long flags;
    if(!wd_parent_get(agdev,1))return;
    spin_lock_irqsave(&agdev->wd_process_lock,flags);
    struct uac2_uevent stop={ACTION_STOP,FORMAT_NONE,BIT_NONE,FREQ_NONE};
    wd_source_error(agdev->wd_source,-ENODEV);
    wd_uac_rate_cancel(agdev,-ENODEV);
    wd_uac_quiesce(1);wd_uac_publish(&agdev->uac2,&stop,SONY_USB_BOUNDARY_DISCONNECT);
#if FEEDBACK_FUNCTION
    usb_ep_disable(agdev->in_ep);agdev->as_in_alt=0;
#endif
    free_ep(&agdev->uac2.c_prm,agdev->out_ep);agdev->as_out_alt=0;

    spin_unlock_irqrestore(&agdev->wd_process_lock,flags);wd_parent_put(agdev);
}

static int
in_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		struct cntrl_cur_lay3 c;

		if (entity_id == USB_IN_CLK_ID)
			c.dCUR = p_srate;
		else if (entity_id == USB_OUT_CLK_ID)
		{
#if FEEDBACK_FUNCTION	
			struct uac2_rtd_params *prm = &uac2->c_prm;
			prm->ep_enabled = true;
#endif
			c.dCUR = c_srate;
		}
		else if (entity_id == USB_OUT_CLK_SEL_ID)
		{
			*(u8 *)req->buf = 1;
			value = min_t(unsigned, w_length, 1);
			return value;
		}
		else return value;

		value = min_t(unsigned, w_length, sizeof c);
		memcpy(req->buf, &c, value);
	} else if (control_selector == UAC2_CS_CONTROL_CLOCK_VALID) {
#if FEEDBACK_FUNCTION	
		struct uac2_rtd_params *prm = &uac2->c_prm;
		prm->ep_enabled = true;
#endif
		*(u8 *)req->buf = 1;
#ifndef CONFIG_USB11_MTK_HDRC
		value = min_t(unsigned, w_length, f_valid);
#else /* CONFIG_USB11_MTK_HDRC */
		value = min_t(unsigned, w_length, 1);
#endif /* CONFIG_USB11_MTK_HDRC */
	} else {
		dev_err(&uac2->pdev.dev,
			"%s:%d control_selector=%d TODO!\n",
			__func__, __LINE__, control_selector);
	}

	return value;
}

static int
in_rq_range(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	//struct cntrl_range_lay3 r;
	int value = -EOPNOTSUPP;

#ifndef CONFIG_ICX_USE_BID
	u8 r_high[]= {
				0x09,0x00,				//wNumSubRanges
#if 0
				0x40,0x1F,0x00,0x00,	//dMIN /* 8000Hz */
				0x40,0x1F,0x00,0x00,	//dMAX /* 8000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 8000Hz */

				0x11,0x2B,0x00,0x00,	//dMIN /* 11025Hz */
				0x11,0x2B,0x00,0x00,	//dMAX /* 11025Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 11025Hz */

				0xE0,0x2E,0x00,0x00,	//dMIN /* 12000Hz */
				0xE0,0x2E,0x00,0x00,	//dMAX /* 12000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 12000Hz */

				0x80,0x3E,0x00,0x00,	//dMIN /* 16000Hz */
				0x80,0x3E,0x00,0x00,	//dMAX /* 16000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 16000Hz */
				
				0x22,0x56,0x00,0x00,	//dMIN /* 22050Hz */
				0x22,0x56,0x00,0x00,	//dMAX /* 22050Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 22050Hz */

				0xC0,0x5D,0x00,0x00,	//dMIN /* 24000Hz */
				0xC0,0x5D,0x00,0x00,	//dMAX /* 24000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 24000Hz */
#endif

				0x00,0x7D,0x00,0x00,	//dMIN /* 32000Hz */
				0x00,0x7D,0x00,0x00,	//dMAX /* 32000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 32000Hz */

				0x44,0xAC,0x00,0x00,	//dMIN /* 44100Hz */
				0x44,0xAC,0x00,0x00,	//dMAX /* 44100Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 44100Hz */

				0x80,0xBB,0x00,0x00,	//dMIN /* 48000Hz */
				0x80,0xBB,0x00,0x00,	//dMAX /* 48000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 48000Hz */

				0x88,0x58,0x01,0x00,	//dMIN /* 88200Hz */
				0x88,0x58,0x01,0x00,	//dMAX /* 88200Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 88200Hz */

				0x00,0x77,0x01,0x00,	//dMIN /* 96000Hz */
				0x00,0x77,0x01,0x00,	//dMAX /* 96000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 96000Hz */

				0x10,0xB1,0x02,0x00,	//dMIN /* 176400Hz */
				0x10,0xB1,0x02,0x00,	//dMAX /* 176400Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 176400Hz */

				0x00,0xEE,0x02,0x00,	//dMIN /* 192000Hz */
				0x00,0xEE,0x02,0x00,	//dMAX /* 192000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 192000Hz */

				0x20,0x62,0x05,0x00,	//dMIN /* 352800Hz */
				0x20,0x62,0x05,0x00,	//dMAX /* 352800Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 352800Hz */

				0x00,0xDC,0x05,0x00,	//dMIN /* 384000Hz */
				0x00,0xDC,0x05,0x00,	//dMAX /* 384000Hz */
				0x00,0x00,0x00,0x00 	//dRES /* 384000Hz */
	};

	u8 r_full[]= {
				0x05,0x00,				//wNumSubRanges

				0x00,0x7D,0x00,0x00,	//dMIN /* 32000Hz */
				0x00,0x7D,0x00,0x00,	//dMAX /* 32000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 32000Hz */

				0x44,0xAC,0x00,0x00,	//dMIN /* 44100Hz */
				0x44,0xAC,0x00,0x00,	//dMAX /* 44100Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 44100Hz */

				0x80,0xBB,0x00,0x00,	//dMIN /* 48000Hz */
				0x80,0xBB,0x00,0x00,	//dMAX /* 48000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 48000Hz */

				0x88,0x58,0x01,0x00,	//dMIN /* 88200Hz */
				0x88,0x58,0x01,0x00,	//dMAX /* 88200Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 88200Hz */

				0x00,0x77,0x01,0x00,	//dMIN /* 96000Hz */
				0x00,0x77,0x01,0x00,	//dMAX /* 96000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 96000Hz */
	};


#ifndef CONFIG_ENABLE_UAC_DSD256
	u8 r_DSD[]= {
				0x02,0x00,				//wNumSubRanges

				0x88,0x58,0x01,0x00,	//dMIN /* 88200Hz */
				0x88,0x58,0x01,0x00,	//dMAX /* 88200Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 88200Hz */

				0x10,0xB1,0x02,0x00,	//dMIN /* 176400Hz */
				0x10,0xB1,0x02,0x00,	//dMAX /* 176400Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 176400Hz */
	};

#else	//for DSD256
	u8 r_DSD[]= {
				0x03,0x00,				//wNumSubRanges

				0x88,0x58,0x01,0x00,	//dMIN /* 88200Hz */
				0x88,0x58,0x01,0x00,	//dMAX /* 88200Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 88200Hz */

				0x10,0xB1,0x02,0x00,	//dMIN /* 176400Hz */
				0x10,0xB1,0x02,0x00,	//dMAX /* 176400Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 176400Hz */

				0x20,0x62,0x05,0x00,	//dMIN /* 352800Hz */
				0x20,0x62,0x05,0x00,	//dMAX /* 352800Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 352800Hz */
	};
#endif
#else //CONFIG_ICX_USE_BID
	u8 r_high_384[]= {
				0x08,0x00,				//wNumSubRanges

				0x44,0xAC,0x00,0x00,	//dMIN /* 44100Hz */
				0x44,0xAC,0x00,0x00,	//dMAX /* 44100Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 44100Hz */

				0x80,0xBB,0x00,0x00,	//dMIN /* 48000Hz */
				0x80,0xBB,0x00,0x00,	//dMAX /* 48000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 48000Hz */

				0x88,0x58,0x01,0x00,	//dMIN /* 88200Hz */
				0x88,0x58,0x01,0x00,	//dMAX /* 88200Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 88200Hz */

				0x00,0x77,0x01,0x00,	//dMIN /* 96000Hz */
				0x00,0x77,0x01,0x00,	//dMAX /* 96000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 96000Hz */

				0x10,0xB1,0x02,0x00,	//dMIN /* 176400Hz */
				0x10,0xB1,0x02,0x00,	//dMAX /* 176400Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 176400Hz */

				0x00,0xEE,0x02,0x00,	//dMIN /* 192000Hz */
				0x00,0xEE,0x02,0x00,	//dMAX /* 192000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 192000Hz */

				0x20,0x62,0x05,0x00,	//dMIN /* 352800Hz */
				0x20,0x62,0x05,0x00,	//dMAX /* 352800Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 352800Hz */

				0x00,0xDC,0x05,0x00,	//dMIN /* 384000Hz */
				0x00,0xDC,0x05,0x00,	//dMAX /* 384000Hz */
				0x00,0x00,0x00,0x00 	//dRES /* 384000Hz */
	};

	u8 r_high_192[]= {
				0x06,0x00,				//wNumSubRanges

				0x44,0xAC,0x00,0x00,	//dMIN /* 44100Hz */
				0x44,0xAC,0x00,0x00,	//dMAX /* 44100Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 44100Hz */

				0x80,0xBB,0x00,0x00,	//dMIN /* 48000Hz */
				0x80,0xBB,0x00,0x00,	//dMAX /* 48000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 48000Hz */

				0x88,0x58,0x01,0x00,	//dMIN /* 88200Hz */
				0x88,0x58,0x01,0x00,	//dMAX /* 88200Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 88200Hz */

				0x00,0x77,0x01,0x00,	//dMIN /* 96000Hz */
				0x00,0x77,0x01,0x00,	//dMAX /* 96000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 96000Hz */

				0x10,0xB1,0x02,0x00,	//dMIN /* 176400Hz */
				0x10,0xB1,0x02,0x00,	//dMAX /* 176400Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 176400Hz */

				0x00,0xEE,0x02,0x00,	//dMIN /* 192000Hz */
				0x00,0xEE,0x02,0x00,	//dMAX /* 192000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 192000Hz */
	};

	u8 r_full[]= {
				0x04,0x00,				//wNumSubRanges

				0x44,0xAC,0x00,0x00,	//dMIN /* 44100Hz */
				0x44,0xAC,0x00,0x00,	//dMAX /* 44100Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 44100Hz */

				0x80,0xBB,0x00,0x00,	//dMIN /* 48000Hz */
				0x80,0xBB,0x00,0x00,	//dMAX /* 48000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 48000Hz */

				0x88,0x58,0x01,0x00,	//dMIN /* 88200Hz */
				0x88,0x58,0x01,0x00,	//dMAX /* 88200Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 88200Hz */

				0x00,0x77,0x01,0x00,	//dMIN /* 96000Hz */
				0x00,0x77,0x01,0x00,	//dMAX /* 96000Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 96000Hz */
	};


	u8 r_DSD_128[]= {
				0x02,0x00,				//wNumSubRanges

				0x88,0x58,0x01,0x00,	//dMIN /* 88200Hz */
				0x88,0x58,0x01,0x00,	//dMAX /* 88200Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 88200Hz */

				0x10,0xB1,0x02,0x00,	//dMIN /* 176400Hz */
				0x10,0xB1,0x02,0x00,	//dMAX /* 176400Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 176400Hz */
	};

	u8 r_DSD_256[]= {
				0x03,0x00,				//wNumSubRanges

				0x88,0x58,0x01,0x00,	//dMIN /* 88200Hz */
				0x88,0x58,0x01,0x00,	//dMAX /* 88200Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 88200Hz */

				0x10,0xB1,0x02,0x00,	//dMIN /* 176400Hz */
				0x10,0xB1,0x02,0x00,	//dMAX /* 176400Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 176400Hz */

				0x20,0x62,0x05,0x00,	//dMIN /* 352800Hz */
				0x20,0x62,0x05,0x00,	//dMAX /* 352800Hz */
				0x00,0x00,0x00,0x00,	//dRES /* 352800Hz */
	};
#endif

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
#if 0		
		if (entity_id == USB_IN_CLK_ID)
			r.dMIN = p_srate;
		else if (entity_id == USB_OUT_CLK_ID)
			r.dMIN = c_srate;
		else
			return -EOPNOTSUPP;

		r.dMAX = r.dMIN;
		r.dRES = 0;
		r.wNumSubRanges = 1;
#endif		

		if (gSPEED == USB_SPEED_HIGH)
		{
			if (DSD_ALT ==  agdev->as_out_alt)
			{
				pr_debug("*************** DSD ALT = %d\n",  agdev->as_out_alt);
				#ifndef CONFIG_ICX_USE_BID
					value = min_t(unsigned, w_length, sizeof r_DSD);
					memcpy(req->buf, &r_DSD, value);
				#else
					switch (check_max_rate(FORMAT_DSD))
					{
						case FREQ_5644800:
							value = min_t(unsigned, w_length, sizeof r_DSD_128);
							memcpy(req->buf, &r_DSD_128, value);
							break;

						case FREQ_11289600:
							value = min_t(unsigned, w_length, sizeof r_DSD_256);
							memcpy(req->buf, &r_DSD_256, value);
							break;

						default:
							value = min_t(unsigned, w_length, sizeof r_DSD_128);
							memcpy(req->buf, &r_DSD_128, value);
					}
				#endif
			}
			else
			{
				#ifndef CONFIG_ICX_USE_BID
					value = min_t(unsigned, w_length, sizeof r_high);
					memcpy(req->buf, &r_high, value);
				#else
					switch (check_max_rate(FORMAT_PCM))
					{
						case FREQ_192000:
							value = min_t(unsigned, w_length, sizeof r_high_192);
							memcpy(req->buf, &r_high_192, value);
							break;

						case FREQ_384000:
							value = min_t(unsigned, w_length, sizeof r_high_384);
							memcpy(req->buf, &r_high_384, value);
							break;

						default:
							value = min_t(unsigned, w_length, sizeof r_high_192);
							memcpy(req->buf, &r_high_192, value);
					}
				#endif

			}
		} else {
			value = min_t(unsigned, w_length, sizeof r_full);
			memcpy(req->buf, &r_full, value);
		}
	} else {
		dev_err(&uac2->pdev.dev,
			"%s:%d control_selector=%d TODO!\n",
			__func__, __LINE__, control_selector);
	}

	return value;
}

static int
ac_rq_in(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	if (cr->bRequest == UAC2_CS_CUR)
		return in_rq_cur(fn, cr);
	else if (cr->bRequest == UAC2_CS_RANGE)
		return in_rq_range(fn, cr);
	else
		return -EOPNOTSUPP;
}

static void uac2_set_complete(struct usb_ep *ep, struct usb_request *req)
{
struct audio_dev *a=NULL;unsigned alt=0;sony_ub_u64 instance=0,generation=0;
    unsigned long flags,pflags;u32 rate=0;int valid,rc=0;
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    valid=req->context==&wd_uac_rate&&wd_uac_rate.pending&&wd_uac_rate.request==req;
    if(valid){alt=wd_uac_rate.alt;instance=wd_uac_rate.instance;generation=wd_uac_rate.generation;
        a=wd_uac_rate_release_locked();}
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    if(!a)return;
    /* Copy shared EP0 data before waiting on the instance serialization lock. */
    if(req->status||req->actual!=sizeof(rate))rc=req->status?req->status:-EPROTO;
    else memcpy(&rate,req->buf,sizeof(rate));
    spin_lock_irqsave(&a->wd_process_lock,pflags);
    spin_lock(&wd_uac_data_lock);
    valid=agdev_g==a&&!a->wd_closing&&(wd_uac_boundary.flags&SONY_USB_BOUNDARY_BOUND)&&
        wd_uac_boundary.instance==instance&&!wd_uac_boundary.error&&a->wd_clock_generation==generation;
    spin_unlock(&wd_uac_data_lock);
    if(valid){
        if(!rc)switch(rate){case 32000:case 44100:case 48000:case 88200:case 96000:
            case 176400:case 192000:case 352800:case 384000:break;default:rc=-EINVAL;}
        if(!rc){c_srate=rate;if(alt)rc=wd_afunc_set_alt_locked(&a->func,a->as_out_intf,alt);else wd_uac_quiesce(0);}
        if(rc)wd_uac_fail(rc);
    }
    spin_unlock_irqrestore(&a->wd_process_lock,pflags);wd_parent_put(a);
}

static int
out_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
struct usb_request *req=fn->config->cdev->req;
    struct audio_dev *agdev=func_to_agdev(fn);
    struct uac2_uevent stop={ACTION_STOP,FORMAT_NONE,BIT_NONE,FREQ_NONE};
    unsigned long flags,pflags;unsigned n;int queued=0,rc;
    if((cr->bRequestType&USB_DIR_IN)||cr->bRequest!=UAC2_CS_CUR||
       (le16_to_cpu(cr->wIndex)>>8)!=USB_OUT_CLK_ID||
       (le16_to_cpu(cr->wValue)>>8)!=UAC2_CS_CONTROL_SAM_FREQ||le16_to_cpu(cr->wLength)!=4)return -EOPNOTSUPP;
    spin_lock_irqsave(&agdev->wd_process_lock,pflags);
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    if(wd_uac_rate.pending){spin_unlock_irqrestore(&wd_uac_data_lock,flags);spin_unlock_irqrestore(&agdev->wd_process_lock,pflags);return -EBUSY;}
    if(agdev_g!=agdev||agdev->wd_closing||agdev->wd_clock_generation==~(u64)0||!(wd_uac_boundary.flags&SONY_USB_BOUNDARY_BOUND)||wd_uac_boundary.error){spin_unlock_irqrestore(&wd_uac_data_lock,flags);spin_unlock_irqrestore(&agdev->wd_process_lock,pflags);return -ENODEV;}
    kref_get(&agdev->wd_refs);wd_uac_rate.generation=++agdev->wd_clock_generation;
    wd_uac_rate.owner=agdev;wd_uac_rate.request=req;wd_uac_rate.previous=req->context;
    wd_uac_rate.previous_complete=req->complete;
    wd_uac_rate.instance=wd_uac_boundary.instance;wd_uac_rate.alt=agdev->as_out_alt;wd_uac_rate.pending=1;
    wd_uac_quiescing=1;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    wd_uac_quiesce(1);rc=wd_uac_publish(&agdev->uac2,&stop,SONY_USB_BOUNDARY_CHANGE);
    if(rc){wd_uac_rate_abort(req,rc);spin_unlock_irqrestore(&agdev->wd_process_lock,pflags);return rc;}
    for(n=0;n<USB_XFERS;++n)queued|=agdev->uac2.c_prm.ureq[n].req!=NULL;
    if(queued||agdev->uac2.c_prm.ep_enabled){
        free_ep(&agdev->uac2.c_prm,agdev->out_ep);
#if FEEDBACK_FUNCTION
        usb_ep_disable(agdev->in_ep);
#endif
    }
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    if(!wd_uac_rate.pending||wd_uac_rate.owner!=agdev||wd_uac_rate.request!=req||
       !(wd_uac_boundary.flags&SONY_USB_BOUNDARY_BOUND)||wd_uac_boundary.instance!=wd_uac_rate.instance){
        spin_unlock_irqrestore(&wd_uac_data_lock,flags);spin_unlock_irqrestore(&agdev->wd_process_lock,pflags);return -ECANCELED;
    }
    req->context=&wd_uac_rate;req->complete=uac2_set_complete;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
#ifndef CONFIG_USB11_MTK_HDRC
    wait_return=1;
#endif
    spin_unlock_irqrestore(&agdev->wd_process_lock,pflags);return 4;
}

static int
setup_rq_inf(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	u16 w_index = le16_to_cpu(cr->wIndex);
	u8 intf = w_index & 0xff;

	if (intf != agdev->ac_intf) {
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
		return -EOPNOTSUPP;
	}

	if (cr->bRequestType & USB_DIR_IN) //get
		return ac_rq_in(fn, cr);
	else if (cr->bRequest == UAC2_CS_CUR)
		return out_rq_cur(fn, cr);

	return -EOPNOTSUPP;
}

static int
wd_afunc_setup_body(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
struct usb_composite_dev *cdev = fn->config->cdev;
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	struct usb_request *req = cdev->req;
	u16 w_length = le16_to_cpu(cr->wLength);
	int value = -EOPNOTSUPP;

	/* Only Class specific requests are supposed to reach here */

	if ((cr->bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS)
		return -EOPNOTSUPP;

	if ((cr->bRequestType & USB_RECIP_MASK) == USB_RECIP_INTERFACE)
		value = setup_rq_inf(fn, cr);
	else
		dev_err(&uac2->pdev.dev, "%s:%d Error!\n", __func__, __LINE__);

	if (value >= 0) {
		req->length = value;
		req->zero = value < w_length;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			dev_err(&uac2->pdev.dev,
				"%s:%d Error!\n", __func__, __LINE__);
			wd_uac_rate_abort(req,value);
			req->status = 0;
		}
	}

	return value;
}
static int afunc_setup(struct usb_function *fn,const struct usb_ctrlrequest *cr)
{
struct audio_dev *a=func_to_agdev(fn);int rc;
    if(!wd_parent_get(a,0))return -ENODEV;
    rc=wd_afunc_setup_body(fn,cr);wd_parent_put(a);return rc;
}


static int audio_bind_config(struct usb_configuration *cfg)
{
int res;struct audio_dev *a;

	a = kzalloc(sizeof *a + USB_XFERS * QMU_PACKAGE_NUM * sizeof(struct usb_iso_packet_descriptor), GFP_KERNEL);
	if (a == NULL) {
		pr_err("Unable to allocate audio gadget\n");
		return -ENOMEM;
	}

    wd_parent_initialize(a);res=wd_parent_publish(a);
    if(res){kfree(a);return res;}
	if (strings_fn[0].id == 0) {

		res = usb_string_ids_tab(cfg->cdev, strings_fn);

		if (res)
		{
			pr_err("[UAC] usb_string_ids_tab fail @Line %d!\n", __LINE__);
			wd_parent_detach(a);return res;
		}
		
		iad_desc.iFunction = strings_fn[STR_ASSOC].id;
		std_ac_if_desc.iInterface = strings_fn[STR_IF_CTRL].id;
#if 0
		in_clk_src_desc.iClockSource = strings_fn[STR_CLKSRC_IN].id;
#endif
		out_clk_src_desc.iClockSource = strings_fn[STR_CLKSRC_OUT].id;
		usb_out_it_desc.iTerminal = strings_fn[STR_USB_IT].id;
#if 0
		io_in_it_desc.iTerminal = strings_fn[STR_IO_IT].id;
#endif
		usb_in_ot_desc.iTerminal = strings_fn[STR_USB_OT].id;
#if 0
		io_out_ot_desc.iTerminal = strings_fn[STR_IO_OT].id;
#endif
		std_as_out_if0_desc.iInterface = strings_fn[STR_AS_OUT_ALT0].id;
		std_as_out_if1_desc.iInterface = strings_fn[STR_AS_OUT_ALT1].id;
		std_as_out_if2_desc.iInterface = strings_fn[STR_AS_OUT_ALT1].id;
		std_as_out_if3_desc.iInterface = strings_fn[STR_AS_OUT_ALT1].id;
		std_as_out_if4_desc.iInterface = strings_fn[STR_AS_OUT_ALT1].id;
		std_as_out_if5_desc.iInterface = strings_fn[STR_AS_OUT_ALT1].id;
		std_as_out_if6_desc.iInterface = strings_fn[STR_AS_OUT_ALT1].id;
		std_as_out_if7_desc.iInterface = strings_fn[STR_AS_OUT_ALT1].id;	
#if 0
		std_as_in_if0_desc.iInterface = strings_fn[STR_AS_IN_ALT0].id;
		std_as_in_if1_desc.iInterface = strings_fn[STR_AS_IN_ALT1].id;
#endif
	}
	
	a->func.name = "uac2_func";
	a->func.strings = fn_strings;
	a->func.bind = afunc_bind;
	a->func.unbind = afunc_unbind;
	a->func.set_alt = afunc_set_alt;
	a->func.get_alt = afunc_get_alt;
	a->func.disable = afunc_disable;
	a->func.setup = afunc_setup;

	/* Initialize the configurable parameters */
	usb_out_it_desc.bNrChannels = num_channels(c_chmask);
	usb_out_it_desc.bmChannelConfig = cpu_to_le32(c_chmask);
#if 0
	io_in_it_desc.bNrChannels = num_channels(p_chmask);
	io_in_it_desc.bmChannelConfig = cpu_to_le32(p_chmask);
#endif	
	as_out_hdr_desc.bNrChannels = num_channels(c_chmask);
	as_out_hdr_desc.bmChannelConfig = cpu_to_le32(c_chmask);
	as_out_raw_hdr_desc.bNrChannels = num_channels(c_chmask);
	as_out_raw_hdr_desc.bmChannelConfig = cpu_to_le32(c_chmask);
#if 0
	as_in_hdr_desc.bNrChannels = num_channels(p_chmask);
	as_in_hdr_desc.bmChannelConfig = cpu_to_le32(p_chmask);
	as_out_fmt1_desc.bSubslotSize = c_ssize;
	as_out_fmt1_desc.bBitResolution = c_ssize * 8;
	as_in_fmt1_desc.bSubslotSize = p_ssize;
	as_in_fmt1_desc.bBitResolution = p_ssize * 8;
#endif
	snprintf(clksrc_in, sizeof(clksrc_in), "%uHz", p_srate);
	snprintf(clksrc_out, sizeof(clksrc_out), "%uHz", c_srate);

	res = usb_add_function(cfg, &a->func);
	if (res < 0)
		wd_parent_detach(a);

	return res;
}

