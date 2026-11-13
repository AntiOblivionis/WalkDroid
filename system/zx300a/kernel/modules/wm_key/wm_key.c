/*
 * icx_key.c  -- ICX Key Driver.
 *
 * Copyright 2009, 2011, 2018 Sony Corporation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/moduleparam.h>

#include <asm/atomic.h>

#include <mach/irqs.h>
#include <mach/eint.h>
#include <cust_eint.h>
#include "mt_auxadc.h"

#include "./icx_key.h"

#include <mach/mt_gpio.h>
#include <mach/mt_gpio_core.h>
#include <mach/mt_idle.h>

#include <mach/icx_pm_helper.h>
#include <mach/icx_key_resume_helper.h>

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

#include <linux/delay.h> // fuji

/* -------------------------------------------------------------------------------- */
/*  definitions                                                                     */
/* -------------------------------------------------------------------------------- */

/* for dedug */
//#define _DEBUG_ICX_KEY_

/* for polling detection of AD keys */
//#define KEY_POLLING_DETECTION

#ifdef _DEBUG_ICX_KEY_
#define TRACE(fmt, ...)     printk("[KEY][L.%d] %s: " fmt , __LINE__, __func__, ## __VA_ARGS__)
#define ERROR(fmt, ...)     printk("[KEY][L.%d] ### ERROR %s: " fmt , __LINE__, __func__, ## __VA_ARGS__)
#else /* _DEBUG_ICX_KEY_ */
#define TRACE(fmt, ...)     do {} while (0)
#define ERROR(fmt, ...)     printk("[KEY] ### ERROR %s: " fmt , __func__, ## __VA_ARGS__)
#endif /* _DEBUG_ICX_KEY_ */

#define ICX_KEY_NAME     "icx_key" // input device name

#ifdef CONFIG_ENABLE_P4BB_KEY
#define NUM_KEY_AD_CH    3    /* three channels */
#define AUX_IN0          0
#define AUX_IN1          1
#define AUX_IN3          3

static u8 adc_ch_key0 = AUX_IN0;
static u8 adc_ch_key1 = AUX_IN1;
static u8 adc_ch_key3 = AUX_IN3;
#elif defined(CONFIG_ENABLE_ICX1300_KEY)
#define NUM_KEY_AD_CH    1    /* one channel: AUX_IN0 */
#define AUX_IN0          0

static u8 adc_ch_key0 = AUX_IN0;
#else
#define NUM_KEY_AD_CH    2    /* two channels: AUX_IN0, AUX_IN1 */
#define AUX_IN0          0
#define AUX_IN1          1

static u8 adc_ch_key0 = AUX_IN0;
static u8 adc_ch_key1 = AUX_IN1;
#endif

// AD key EINT number setting for module_param
// actual EINT number of AD key will be set by insmod timing
static int   eint_num_key_wake = 0;
static int   eint_num_hold = 0;
static int   eint_num_nc = 0;

#define EINT_NUM_6       6    // EINT 6
#define EINT_NUM_7       7    // EINT 7
#define EINT_NUM_156     156  // EINT 156
#define EINT_NUM_157     157  // EINT 157
#ifdef CONFIG_ENABLE_P4BB_KEY
#define EINT_NUM_163     163
#endif

// GPIO key settings for HOLD key
static int   gpio_num_hold = 0;
// GPIO key settings for NC key
static int   gpio_num_nc = 0;


#define GPIO_NUM_9       9    // GPIO 9
#define GPIO_NUM_29      29   // GPIO 29

// GPIO key settings for NC(Noise Cancelling) key
#define GPIO_NUM_8       8    // GPIO 8

// EINT status bit position (0 - 5) of icx_pm_helper_spm_stat
static uint32_t eint_sta_position_key_wake = 0;
static uint32_t eint_sta_position_hold     = 0;
static uint32_t eint_sta_position_nc       = 0;

// EINT status bit allocation
static uint32_t eint_sta_key_wake = 0;
static uint32_t eint_sta_hold     = 0;
static uint32_t eint_sta_nc       = 0;


/* -------------------------------------------------------------------------------- */
/*  module_param macro setings                                                      */
/*  setting a EINT number of key wake when the module is loaded                     */
/* -------------------------------------------------------------------------------- */
module_param(eint_num_key_wake, int, S_IRUGO);

/* -------------------------------------------------------------------------------- */
/*  module_param macro setings                                                      */
/*  For providing the EINT information to the user space                            */
/* -------------------------------------------------------------------------------- */
module_param(eint_num_hold, int, S_IRUGO );
module_param(eint_num_nc, int, S_IRUGO );

// for module_param_cb
static int   hold_key_value = 0;
static int   nc_key_value = 0;

typedef struct {
  int key_ad[NUM_KEY_AD_CH];  /* key_ad contains index of ad_val_table[] */
  int key_hold;
  int key_nc;
} key_state_t;

typedef struct {
  struct input_dev *dev;
  struct delayed_work dwork_chat;
  struct delayed_work dwork;
  
  key_state_t key_state;
  
  int suspend_flag;
#define SUSPEND_FLAG_ENABLED    1
#define SUSPEND_FLAG_DISABLED   0
  
  atomic_t busy_flag;
#define BUSY_FLAG_ENABLED       1
#define BUSY_FLAG_DISABLED      0
  
  atomic_t poll_flag;
#define POLL_FLAG_ENABLED       1
#define POLL_FLAG_DISABLED      0
  
  atomic_t workq_flag;
#define WORKQ_FLAG_ENABLED       1
#define WORKQ_FLAG_DISABLED      0

  atomic_t active_flag;
#define ACTIVE_FLAG_ACTIVE      1
#define ACTIVE_FLAG_INACTIVE    0
} icx_key_dev_t;

static icx_key_dev_t 
icx_key_dev;

#define SCAN_INTERVAL_CHAT_GUARD    10  // msec
#define SCAN_INTERVAL_UNPUSH_DETECT 50  // msec
#if defined(KEY_POLLING_DETECTION) || defined(CONFIG_ENABLE_P4BB_KEY)
#define SCAN_INTERVAL_AD_POLLING    100 // msec
#endif // KEY_POLLING_DETECTION

/* chattering parameters for AD key */
#define CHAT_PUSH_CNT   1
#define CHAT_TRIAL_CNT  4

/* chattering parameters for GPIO key */
#define GPIO_CHAT_PUSH_CNT   1
#define GPIO_CHAT_TRIAL_CNT  2

#ifdef CONFIG_ENABLE_P4BB_KEY
static const u32 ad_val_table[] = {
    // for PF and Walkman AD key specification
    0x200, 0x600, 0xFFF
};
#else
static const u32 ad_val_table[] = {
   // for PF and Walkman AD key specification
   0xDF, 0x279, 0x46F, 0x67C, 0x8CC, 0xB67, 0xED3, 0xFFF
};
#endif

#define NUM_KEY_INDEX           (ARRAY_SIZE(ad_val_table))
#define KEY_INDEX_UNPUSH        NUM_KEY_INDEX

/* AD KEY */
#ifdef CONFIG_ENABLE_P4BB_KEY
/* AD0 */
#define KC_AD0_0          KEY_POWER
#define KC_AD0_1          KEY_POWER
/* AD1 */
#define KC_AD1_0          KEY_MENU
#define KC_AD1_1          KEY_BLUETOOTH
/* AD3 */
#define KC_AD3_0          KEY_VOLUMEUP
#define KC_AD3_1          KEY_VOLUMEDOWN
#else
/* AD0 */
#define KC_AD0_0          KEY_ENTER
#define KC_AD0_1          KEY_RIGHT
#define KC_AD0_2          KEY_LEFT
#define KC_AD0_3          KEY_BACK
#define KC_AD0_4          KEY_DOWN
#define KC_AD0_5          KEY_HOME
#define KC_AD0_6          KEY_F6
#define KC_AD0_7          KEY_POWER
/* AD1 */
#define KC_AD1_0          KEY_VOLUMEDOWN
#define KC_AD1_1          KEY_VOLUMEUP
#define KC_AD1_2          KEY_UP
#define KC_AD1_3          KEY_MENU
#define KC_AD1_4          KEY_BLUETOOTH
#define KC_AD1_5          KEY_F15
#define KC_AD1_6          KEY_F16
#define KC_AD1_7          KEY_F17
#endif

/* GPIO KEY */
#define KC_HOLD           KEY_H
#define KC_NC             KEY_N

/* resume notification to middleware*/
#define KC_WAKEUP         KEY_WAKEUP
/* reserved key */
#define KC_KEY_NONE       KEY_RESERVED

#define INPUT_EVENT_UP    ICX_KEY_VALUE_UP
#define INPUT_EVENT_DOWN  ICX_KEY_VALUE_DOWN

/* Key code for AD_KEYs */
#ifdef CONFIG_ENABLE_P4BB_KEY
static const u32 icx_key_code[NUM_KEY_AD_CH][NUM_KEY_INDEX] = {
  { KC_AD0_0, KC_AD0_1, KC_KEY_NONE, }, /* KEY_AD0 */
  { KC_AD1_0, KC_AD1_1, KC_KEY_NONE, }, /* KEY_AD1 */
  { KC_AD3_0, KC_AD3_1, KC_KEY_NONE, }, /* KEY_AD3 */
};
#elif defined(CONFIG_ENABLE_ICX1300_KEY)
static const u32 icx_key_code[NUM_KEY_AD_CH][NUM_KEY_INDEX] = {
  { KC_AD0_0, KC_AD0_1, KC_AD0_2, KC_AD0_3, KC_AD0_4, KC_AD0_5, KC_AD0_6, KC_KEY_NONE, }, /* KEY_AD0 */
};
#else
static const u32 icx_key_code[NUM_KEY_AD_CH][NUM_KEY_INDEX] = {
  { KC_AD0_0, KC_AD0_1, KC_AD0_2, KC_AD0_3, KC_AD0_4, KC_AD0_5, KC_AD0_6, KC_KEY_NONE, }, /* KEY_AD0 */
  { KC_AD1_0, KC_AD1_1, KC_AD1_2, KC_AD1_3, KC_AD1_4, KC_AD1_5, KC_AD1_6, KC_KEY_NONE, }, /* KEY_AD1 */
};
#endif

/* total key code for both ADs key and GPIO keys*/
#ifdef CONFIG_ENABLE_P4BB_KEY
static const u32 total_icx_key_code_list[] = {
  KEY_H, KEY_N
};
#elif defined(CONFIG_ENABLE_ICX1300_KEY)
static const u32 total_icx_key_code_list[] = {
  KC_AD0_0, KC_AD0_1, KC_AD0_2,
};
#else
static const u32 total_icx_key_code_list[] = {
  KC_AD0_0, KC_AD0_1, KC_AD0_2, KC_AD0_3, KC_AD0_4, KC_AD0_5, KC_AD0_6, 
  KC_AD1_0, KC_AD1_1, KC_AD1_2, KC_AD1_3, KC_AD1_4, KC_AD1_5, KC_AD1_6, 
  KEY_H, KEY_N
};
#endif

static u32 resume_ad_key_ch = 0xFF;
static u32 resume_ad_key    = 0xFFF;
static u32 resume_gpio_key  = 0xFFF;

static int key_state_hold(void)
{
#ifdef CONFIG_DISABLE_HOLD_KEY
  hold_key_value = 1;
#else
  hold_key_value = mt_get_gpio_in(gpio_num_hold);
#endif
  TRACE("key_state_hold is %d\n", hold_key_value);
  
  return hold_key_value;
}

static int key_state_nc(void)
{
  nc_key_value = mt_get_gpio_in(gpio_num_nc);
  TRACE("key_state_nc is %d\n", nc_key_value);
  
  return nc_key_value;
}

int get_hold_key_value(char *buffer, const struct kernel_param *kp)
{
  // 1 is set to the parameter buffer when hold key status is 1 (ON)
  // 0 is set to the parameter buffer when hold key status is 0 (OFF)
#ifdef CONFIG_DISABLE_HOLD_KEY
  return sprintf(buffer, "1");
#else
  return sprintf(buffer, "%c", mt_get_gpio_in(gpio_num_hold) == 1 ? '1' : '0');
#endif
}

int get_nc_key_value(char *buffer, const struct kernel_param *kp)
{
  // 1 is set to the parameter buffer when nc key status is 1 (ON)
  // 0 is set to the parameter buffer when nc key status is 0 (OFF)
  return sprintf(buffer, "%c", mt_get_gpio_in(gpio_num_nc) == 1 ? '1' : '0');
}

static struct kernel_param_ops param_hold_str = {
  .get = get_hold_key_value,
};

static struct kernel_param_ops param_nc_str = {
  .get = get_nc_key_value,
};

module_param_cb(hold_key_value, &param_hold_str, &hold_key_value, 0644);
MODULE_PARM_DESC(hold_key_value, "current hold key condition value.");

module_param_cb(nc_key_value, &param_nc_str, &nc_key_value, 0644);
MODULE_PARM_DESC(nc_key_value, "current nc key condition value.");

static void reset_key_state(void)
{
  int ch;
  icx_key_dev_t *dev = &icx_key_dev;
  
  for (ch = 0; ch < NUM_KEY_AD_CH; ch++)
    dev->key_state.key_ad[ch] = KEY_INDEX_UNPUSH;     /* ad key: released by default. */
  
  switch (eint_num_key_wake) {
#ifdef CONFIG_ENABLE_P4BB_KEY
    case EINT_NUM_163:
      pr_info("[P4BB]eint not set!!");
      break;
#endif
    case EINT_NUM_6: // this EINT setting is for WM DTB
      gpio_num_hold = GPIO_NUM_29;
      eint_num_hold = EINT_NUM_7;
      dev->key_state.key_hold = key_state_hold();
      eint_sta_position_key_wake = 0;
      eint_sta_position_hold     = 0;
      eint_sta_key_wake = 0x40;
      eint_sta_hold     = 0x80;
      break;
    case EINT_NUM_7: // this EINT setting is for PF DTB
    default:
      gpio_num_hold = GPIO_NUM_9;
      eint_num_hold = EINT_NUM_157;
      gpio_num_nc   = GPIO_NUM_8;
      eint_num_nc   = EINT_NUM_156;
      dev->key_state.key_hold = key_state_hold();
      dev->key_state.key_nc   = key_state_nc();
      eint_sta_position_key_wake = 0;
      eint_sta_position_hold     = 4;
      eint_sta_position_nc       = 4;
      eint_sta_key_wake = 0x80;
      eint_sta_hold     = 0x20000000;
      eint_sta_nc       = 0x10000000;
      break;
  }
}

static inline int index_from_ad_val(u32 ad_val)
{
  int i;
  
  for (i = 0; i < ARRAY_SIZE(ad_val_table); i++)
    if (ad_val < ad_val_table[i])
      break;
  
  TRACE("+++ index = %d\n", i);
  return i;
}

static inline u32 key_code_from_index(int ch, int key_index)
{
#ifdef CONFIG_ENABLE_P4BB_KEY
  u32 code = icx_key_code[ch][key_index];
  printk(" ch=%d index=%d key_code=0x%x(%s)\n", ch, key_index, code,
			(code == KEY_POWER) ? "POWER":
			(code == KEY_MENU) ?  "INPUT":
			(code == KEY_BLUETOOTH ) ? "PAIRING":
			(code == KEY_VOLUMEDOWN ) ? "VOL -":
			(code == KEY_VOLUMEUP) ? "VOL +": "other");
	return code;
#else
  TRACE(" ch=%d index=%d key_code=0x%x\n", ch, key_index, icx_key_code[ch][key_index]);
  return icx_key_code[ch][key_index];
#endif
}

static void key_sleep(int timeout)
{
  set_current_state(TASK_INTERRUPTIBLE);
  schedule_timeout(timeout);
}

static u32 get_key_mask(void)
{
  return 0;
}

static int get_md_adc_val(unsigned int num)
{
  int data[4] = { 0, 0, 0, 0 };
  int val = 0;
  int ret = 0;
  
  ret = IMM_GetOneChannelValue(num, data, &val);
  
  TRACE("GetOneChannelValue: ret=%d, num=%d, val=%d\n", ret, num, val);
  TRACE("GetOneChannelValue: data= %d, %d, %d, %d\n", data[0], data[1], data[2], data[3]);
  
  if (ret == 0)
    return val;
  else
    return -1;
}


static void send_input_event_for_adkey_interrupt(int key_index[])
{
  int ch;
  icx_key_dev_t *dev = &icx_key_dev;
  
  for (ch = 0; ch < NUM_KEY_AD_CH; ch++) {
    if (key_index[ch] != dev->key_state.key_ad[ch]) {
      TRACE("CH%d status CHANGED!\n", ch);
      if (dev->key_state.key_ad[ch] == KEY_INDEX_UNPUSH) {
        printk(" key Down (1) ch=%d index=%d key_code=0x%x\n", ch, key_index[ch], key_code_from_index(ch, key_index[ch]));
        /* send DOWN_EVENT for new key to input subsystem. */
        input_event(dev->dev,
                    EV_KEY,
                    key_code_from_index(ch, key_index[ch]) | get_key_mask(),
                    INPUT_EVENT_DOWN);
        input_sync( dev->dev );
      } else {
        printk(" key Up ch=%d index=%d key_code=0x%x\n", ch, dev->key_state.key_ad[ch], key_code_from_index(ch, dev->key_state.key_ad[ch]));
        /* send UP_EVENT for former key to input subsystem. */
        input_event(dev->dev,
                    EV_KEY,
                    key_code_from_index(ch, dev->key_state.key_ad[ch]) | get_key_mask(),
                    INPUT_EVENT_UP);
        input_sync( dev->dev );
        if (key_index[ch] != KEY_INDEX_UNPUSH) {
          printk(" key Down (2) ch=%d index=%d key_code=0x%x\n", ch, key_index[ch], key_code_from_index(ch, key_index[ch]));
          /* send DOWN_EVENT for new key to input subsystem. */
          input_event(dev->dev,
                      EV_KEY,
                      key_code_from_index(ch, key_index[ch]) | get_key_mask(),
                      INPUT_EVENT_DOWN);
          input_sync( dev->dev );
        } else {
          /* do nothing. */
          ;
        }
      }
      /* update key code. */
      dev->key_state.key_ad[ch] = key_index[ch];
  }
#ifdef _DEBUG_ICX_KEY_
    else {
      TRACE("Key Status (CH%d) Un-Changed!\n", ch);
    }
#endif /* _DEBUG_ICX_KEY_ */
  }

}

/* -------------------------------------------------------------------------------- */
/*  get ad_key of resume                                                            */
/* -------------------------------------------------------------------------------- */
static void get_resume_ad_key(void)
{
  struct icx_pm_helper_spm_stat *sps;
  struct icx_key_spm_stat *kss;
  int ch;

  sps = &icx_pm_helper_spm_stat;
  kss = &icx_key_spm_stat;
  resume_ad_key_ch = 0xFF;
  resume_ad_key    = 0xFFF;

  /* AD Key */
//  TRACE("The system resumed by AD Key \n");
//  TRACE("kss->key_ad_sta[eint_sta_position_key_wake] = %d\n", kss->key_ad_sta[eint_sta_position_key_wake]);
//  TRACE("kss->key_ad_sta[eint_sta_position_key_wake] = %d\n", kss->key_ad_sta[eint_sta_position_key_wake]);

  for (ch = 0; ch < NUM_KEY_AD_CH; ch++) {
    if (kss->key_ad_sta[ch] < 4095) {
      resume_ad_key_ch = ch;
      resume_ad_key    = kss->key_ad_sta[ch];
      kss->key_ad_sta[ch] = 0xFFF;
      TRACE("resume_ad_key_ch = %d\n", resume_ad_key_ch);
      TRACE("resume_ad_key     = %d\n", resume_ad_key);
    }
  }
}

#ifdef DPIDLE_CAN_ENTER_EXTRA_STATUS
static int cdev_icx_key_is_active(void)
{
  if ((atomic_read(&icx_key_dev.active_flag) == ACTIVE_FLAG_ACTIVE)
   || (atomic_read(&icx_key_dev.poll_flag)   == POLL_FLAG_ENABLED))
    return 1;
  else
    return 0;
}
#endif

// character device settings for ioctl
#define DRV_NAME        "icx_key" // character device name
static dev_t            m_devid ;
static struct cdev      m_cdev ;
static struct class     *m_p_class ;

/* -------------------------------------------------------------------------------- */
/*  character device open                                                           */
/* -------------------------------------------------------------------------------- */
static int cdev_icx_key_open(struct inode *p_inode, struct file *p_file)
{
  TRACE("\e[92m******** %s ********\e[0m\n", __func__) ;
  
  return 0;
}

/* -------------------------------------------------------------------------------- */
/*  character device close                                                          */
/* -------------------------------------------------------------------------------- */
static ssize_t cdev_icx_key_release(struct inode *p_inode, struct file *p_file)
{
  TRACE("\e[92m******** %s ********\e[0m\n", __func__) ;
  
  return 0;
}

/* -------------------------------------------------------------------------------- */
/*  character device read                                                           */
/* -------------------------------------------------------------------------------- */
static ssize_t cdev_icx_key_read(struct file *p_file, char __user *p_buff, size_t count, loff_t *p_pos)
{
  TRACE("\e[92m******** %s ********\e[0m\n", __func__) ;
  
  return 0;
}

/* -------------------------------------------------------------------------------- */
/*  character device write                                                          */
/* -------------------------------------------------------------------------------- */
static ssize_t cdev_icx_key_write(struct file *p_file, const char __user *p_buff, size_t count, loff_t *p_pos)
{
  TRACE("\e[92m******** %s ********\e[0m\n", __func__) ;
  
  return 0;
}

static long cdev_icx_key_ioctl(struct file *p_file, unsigned int cmd, unsigned long arg)
{
  int key_cur[NUM_KEY_AD_CH];
  
  switch( cmd ) {
    case ICX_KEY_IOCTL_CMD_GETCOUNT:
    {
      return sizeof( total_icx_key_code_list ) / sizeof( int );
    }
    
    case ICX_KEY_IOCTL_CMD_GETINFO:
    {
      if( copy_to_user((void __user *)arg, &total_icx_key_code_list[0], sizeof( total_icx_key_code_list )) ) {
        return -EFAULT ; // Bad address (POSIX.1)
      }
      break ;
    }
    
    case ICX_KEY_IOCTL_CMD_GET_RESUME_ADKEY:
    {
      get_resume_ad_key();
      if (resume_ad_key_ch == 0xFF) {
        return -EFAULT ; // Bad address (POSIX.1)
      }
      key_cur[resume_ad_key_ch] = index_from_ad_val(resume_ad_key);
      return key_code_from_index(resume_ad_key_ch, key_cur[resume_ad_key_ch]);
    }
    
    case ICX_KEY_IOCTL_CMD_GET_RESUME_GPIOKEY: // no use
    {
      // gpio key event is sent as input event if gpio key is pressed in warm sleep
      if (resume_gpio_key == 0xFFF) {
        return -EFAULT ; // Bad address (POSIX.1)
      }
      return resume_gpio_key;
    }
    
    case ICX_KEY_IOCTL_CMD_GET_RESUME_KEY:
    {
      get_resume_ad_key();
      // no need to set resumed gpio key information, 
      // because gpio key event is sent as input event if gpio key is pressed in warm sleep
      if (resume_ad_key_ch == 0xFF) {
        if (resume_gpio_key == 0xFFF) {
          //printk("***** EFAULT\n");
          return -EFAULT ; // Bad address (POSIX.1)
        } else {
          //printk("***** resume_gpio_key = %d\n", resume_gpio_key);
          return resume_gpio_key;
        }
      } else {
        key_cur[resume_ad_key_ch] = index_from_ad_val(resume_ad_key);
        //printk("***** resume_ad_key = %d\n", key_code_from_index(resume_ad_key_ch, key_cur[resume_ad_key_ch]));
        return key_code_from_index(resume_ad_key_ch, key_cur[resume_ad_key_ch]);
      }
    }
    
    default:
      return -EINVAL ; // invalid argument
  }

  return 0 ;
}

static struct file_operations icx_key_fops={
  .open           = cdev_icx_key_open,
  .release        = cdev_icx_key_release,
  .read           = cdev_icx_key_read,
  .write          = cdev_icx_key_write,
  .unlocked_ioctl = cdev_icx_key_ioctl,
  .compat_ioctl   = cdev_icx_key_ioctl,
};

static void workq_func(struct work_struct *work)
{
  int i;
  int ch;
  //unsigned char ad_val[NUM_KEY_AD_CH];
  int ad_val[NUM_KEY_AD_CH];
  int key_cur[NUM_KEY_AD_CH];
#ifdef CONFIG_ENABLE_P4BB_KEY
  int key_prev[NUM_KEY_AD_CH] = { -1, -1, -1 };
#elif defined(CONFIG_ENABLE_ICX1300_KEY)
  int key_prev[NUM_KEY_AD_CH] = { -1 };
#else
  int key_prev[NUM_KEY_AD_CH] = { -1, -1 };
#endif
  int push_cnt = 0;
  icx_key_dev_t *dev = &icx_key_dev;
  
  atomic_set(&icx_key_dev.active_flag, ACTIVE_FLAG_ACTIVE);
  for (i = 0; i < CHAT_TRIAL_CNT; i++) {
    TRACE("chattering guard trial count=%d\n", i);
    
    /* chattering guard interval */
    if (i != 0)
      key_sleep(msecs_to_jiffies(SCAN_INTERVAL_CHAT_GUARD));
    
    // reading ADIN0 value
    ad_val[0] = get_md_adc_val(adc_ch_key0);
    
    if (ad_val[0] < 0) {
      ERROR("get_md_adc_val(ADIN%d) failed\n", adc_ch_key0 + 1);
      atomic_set(&icx_key_dev.active_flag, ACTIVE_FLAG_INACTIVE);
      return;
    }
    
    TRACE("ad_val[0]=%d\n", ad_val[0]);

#ifndef CONFIG_ENABLE_ICX1300_KEY
    // reading ADIN1 value
    ad_val[1] = get_md_adc_val(adc_ch_key1);
    
    if (ad_val[1] < 0) {
      ERROR("get_md_adc_val(ADIN%d) failed\n", adc_ch_key1 + 1);
      atomic_set(&icx_key_dev.active_flag, ACTIVE_FLAG_INACTIVE);
      return;
    }

    TRACE("ad_val[1]=%d\n", ad_val[1]);
#endif

#ifdef CONFIG_ENABLE_P4BB_KEY
    // reading ADIN3 value
    ad_val[2] = get_md_adc_val(adc_ch_key3);
    if (ad_val[2] < 0) {
      ERROR("get_md_adc_val(ADIN%d) failed\n", adc_ch_key3 + 1);
      atomic_set(&icx_key_dev.active_flag, ACTIVE_FLAG_INACTIVE);
      return;
    }
#endif
    
    for (ch = 0; ch < NUM_KEY_AD_CH; ch++)
      key_cur[ch] = index_from_ad_val(ad_val[ch]);
    
#ifdef CONFIG_ENABLE_P4BB_KEY
    push_cnt = (key_cur[0] == key_prev[0] && key_cur[1] == key_prev[1] &&
                key_cur[2] == key_prev[2])?
      push_cnt + 1: 0;
#elif defined(CONFIG_ENABLE_ICX1300_KEY)
    TRACE("index=[%d]\n", key_cur[0]);

    push_cnt = (key_cur[0] == key_prev[0])?
      push_cnt + 1: 0;
#else
    TRACE("index=[%d %d]\n", key_cur[0], key_cur[1]);

    push_cnt = (key_cur[0] == key_prev[0] && key_cur[1] == key_prev[1])?
      push_cnt + 1: 0;
#endif
        
    if (push_cnt >= CHAT_PUSH_CNT) {
      break;
    }
    
    key_prev[0] = key_cur[0];
#ifndef CONFIG_ENABLE_ICX1300_KEY
    key_prev[1] = key_cur[1];
#endif
#ifdef CONFIG_ENABLE_P4BB_KEY
    key_prev[2] = key_cur[2];
#endif
  }
  atomic_set(&icx_key_dev.active_flag, ACTIVE_FLAG_INACTIVE);
  
  if (i >= CHAT_TRIAL_CNT) {
    /* key chattering */
    //if (atomic_read(&icx_key_dev.poll_flag) == POLL_FLAG_ENABLED) {
    if ((atomic_read(&icx_key_dev.poll_flag) == POLL_FLAG_ENABLED) && (atomic_read(&icx_key_dev.workq_flag) == WORKQ_FLAG_ENABLED)) {
      /* because the key chattering is detected during key_release polling, the 
        polling must be continued. */
      ERROR("chattering detected during key release polling !\n");
      schedule_delayed_work(&dev->dwork_chat, msecs_to_jiffies(SCAN_INTERVAL_UNPUSH_DETECT));
    } else {
      /* because the key chattering is detected at the first key check from 
        KEY_WAKE interrut, the key code must be thrown away. */
      ERROR("chattering detected at key press interrupt !\n");
      #if defined(KEY_POLLING_DETECTION) || defined(CONFIG_ENABLE_P4BB_KEY)
      schedule_delayed_work(&icx_key_dev.dwork, msecs_to_jiffies(SCAN_INTERVAL_AD_POLLING));
      #endif // KEY_POLLING_DETECTION
      return;
    }
  
  } else {
    
    send_input_event_for_adkey_interrupt(key_cur);
    
#ifdef CONFIG_ENABLE_P4BB_KEY
    if (key_cur[0] == KEY_INDEX_UNPUSH && key_cur[1] == KEY_INDEX_UNPUSH &&
        key_cur[2] == KEY_INDEX_UNPUSH) {
#elif defined(CONFIG_ENABLE_ICX1300_KEY)
    if (key_cur[0] == KEY_INDEX_UNPUSH) {
#else
    if (key_cur[0] == KEY_INDEX_UNPUSH && key_cur[1] == KEY_INDEX_UNPUSH) {
#endif
      /* ad_key's released just now, so stop polling for detecting key release. */
      TRACE("RELEASED!\n");
      atomic_set(&icx_key_dev.busy_flag, BUSY_FLAG_DISABLED);
      atomic_set(&icx_key_dev.poll_flag, POLL_FLAG_DISABLED);
      #if defined(KEY_POLLING_DETECTION) || defined(CONFIG_ENABLE_P4BB_KEY)
      schedule_delayed_work(&icx_key_dev.dwork, msecs_to_jiffies(SCAN_INTERVAL_AD_POLLING));
      #endif // KEY_POLLING_DETECTION
    } else {
      TRACE("PRESSED!\n");
      /* start polling for detecting key release. */
      atomic_set(&icx_key_dev.poll_flag, POLL_FLAG_ENABLED);
      if (atomic_read(&icx_key_dev.workq_flag) == WORKQ_FLAG_ENABLED) {
        schedule_delayed_work(&dev->dwork_chat, msecs_to_jiffies(SCAN_INTERVAL_UNPUSH_DETECT));
      }
    }
  }
}

static void key_wake_irq_handler(void)
{
  TRACE("KEY_WAKE interrupt!\n");
  
  /* temporary
  if (icx_key_dev.suspend_flag == SUSPEND_FLAG_ENABLED) {
    TRACE("suspend on\n");
  }
  */ 
  
  atomic_set(&icx_key_dev.busy_flag, BUSY_FLAG_ENABLED);
  
  /* going to scan ad_key right away. */
  if (atomic_read(&icx_key_dev.workq_flag) == WORKQ_FLAG_ENABLED) {
    schedule_delayed_work(&icx_key_dev.dwork, 0);
  }
}

#ifndef CONFIG_DISABLE_HOLD_KEY
static void key_xhold_irq_handler(void)
{
  int i;
  int hold_key_cur;
  int hold_key_prev = -1;
  int push_cnt = 0;
  icx_key_dev_t *dev = &icx_key_dev;
  
  TRACE("KEY_XHOLD interrupt!\n");
  
  for (i = 0; i < GPIO_CHAT_TRIAL_CNT; i++) {
    hold_key_cur = key_state_hold();
    //push_cnt++;
    push_cnt = (hold_key_cur == hold_key_prev) ? push_cnt + 1: 0;
    if (push_cnt >= GPIO_CHAT_PUSH_CNT) {
      break;
    }
    hold_key_prev = hold_key_cur;
  }
  
  if (i >= GPIO_CHAT_TRIAL_CNT) {
    ERROR("chattering detected at key press interrupt !\n");
    return;
  
  } else {
    if (dev->key_state.key_hold != hold_key_cur) {
      /*
      HOLD Key is
      High(1) : OFF
      Low (0) : ON
      */
      if (hold_key_cur == 1) {
        printk(" hold Off (Up)\n");
        input_event(dev->dev, EV_KEY, KC_HOLD, INPUT_EVENT_UP);
        mt_eint_registration(eint_num_hold, EINTF_TRIGGER_LOW, key_xhold_irq_handler, 1);
      } else {
        printk(" hold On (Down)\n");
        input_event(dev->dev, EV_KEY, KC_HOLD, INPUT_EVENT_DOWN);
        mt_eint_registration(eint_num_hold, EINTF_TRIGGER_HIGH, key_xhold_irq_handler, 1);
      }
      input_sync( dev->dev );
      dev->key_state.key_hold = hold_key_cur;
    }
  }
  
}
#endif

static void key_nc_irq_handler(void)
{
  int i;
  int nc_key_cur;
  int nc_key_prev = -1;
  int push_cnt = 0;
  icx_key_dev_t *dev = &icx_key_dev;
  
  TRACE("KEY_XHOLD interrupt!\n");
  
  for (i = 0; i < GPIO_CHAT_TRIAL_CNT; i++) {
    nc_key_cur = key_state_nc();
    //push_cnt++;
    push_cnt = (nc_key_cur == nc_key_prev) ? push_cnt + 1: 0;
    if (push_cnt >= GPIO_CHAT_PUSH_CNT) {
      break;
    }
    nc_key_prev = nc_key_cur;
  }
  
  if (i >= GPIO_CHAT_TRIAL_CNT) {
    ERROR("chattering detected at key press interrupt !\n");
    return;
  
  } else {
    if (dev->key_state.key_nc != nc_key_cur) {
      /*
      NC Key is
      High(1) : OFF
      Low (0) : ON
      */
      if (nc_key_cur == 1) {
        printk(" nc Off (Up)\n");
        input_event(dev->dev, EV_KEY, KC_NC, INPUT_EVENT_UP);
        mt_eint_registration(eint_num_nc, EINTF_TRIGGER_LOW, key_nc_irq_handler, 1);
      } else {
        printk(" nc On (Down)\n");
        input_event(dev->dev, EV_KEY, KC_NC, INPUT_EVENT_DOWN);
        mt_eint_registration(eint_num_nc, EINTF_TRIGGER_HIGH, key_nc_irq_handler, 1);
      }
      input_sync( dev->dev );
      dev->key_state.key_nc = nc_key_cur;
    }
  }
  
}

#ifdef CONFIG_ENABLE_P4BB_KEY
static void key_xPower_irq_handler(void)
{
  printk("ad-key(power) interrupt!!\n");

  atomic_set(&icx_key_dev.busy_flag, BUSY_FLAG_ENABLED);
  
  /* going to scan ad_key right away. */
  if (atomic_read(&icx_key_dev.workq_flag) == WORKQ_FLAG_ENABLED) {
    schedule_delayed_work(&icx_key_dev.dwork, 0);
  }
}
#endif

static void icx_key_end_processing(void)
{
  TRACE("icx_key: end_processing");
  
#ifdef DPIDLE_CAN_ENTER_EXTRA_STATUS
  register_dpidle_key_status_func(NULL);
#endif

  if (atomic_read(&icx_key_dev.workq_flag) == WORKQ_FLAG_ENABLED) {
    if (eint_num_key_wake == EINT_NUM_7) { // for only PF DTB
      mt_eint_mask(eint_num_nc);
    }
#ifndef CONFIG_ENABLE_P4BB_KEY
#ifndef CONFIG_DISABLE_HOLD_KEY
    mt_eint_mask(eint_num_hold);
#endif
#endif
    mt_eint_mask(eint_num_key_wake);
    
    atomic_set(&icx_key_dev.workq_flag, WORKQ_FLAG_DISABLED);
    atomic_set(&icx_key_dev.busy_flag, BUSY_FLAG_DISABLED);
    atomic_set(&icx_key_dev.poll_flag, POLL_FLAG_DISABLED);
    icx_key_dev.suspend_flag = SUSPEND_FLAG_DISABLED;
    
    /* cancelling workq */
    cancel_delayed_work_sync(&icx_key_dev.dwork);
    cancel_delayed_work_sync(&icx_key_dev.dwork_chat);
    
    /* unregister character device */
    device_destroy(m_p_class, m_devid) ;
    class_destroy( m_p_class ) ;
    cdev_del( &m_cdev ) ;
    unregister_chrdev_region(m_devid, 1) ;
    
    /* unregister input device*/
    input_unregister_device(icx_key_dev.dev);
    
    /* unregister EINT settings */
    if (eint_num_key_wake == EINT_NUM_7) { // for only PF DTB
      mt_eint_registration(eint_num_nc, EINTF_TRIGGER_HIGH, NULL, 0);
    }
    
#ifdef CONFIG_ENABLE_P4BB_KEY
    mt_eint_registration(eint_num_key_wake, EINTF_TRIGGER_HIGH, NULL, 0);
#else
#ifndef CONFIG_DISABLE_HOLD_KEY
    mt_eint_registration(eint_num_hold, EINTF_TRIGGER_HIGH, NULL, 0);
#endif
    
    mt_eint_registration(eint_num_key_wake, EINTF_TRIGGER_RISING, NULL, 0);
#endif
  }
}

/* -------------------------------------------------------------------------------- */
/*  input device open                                                               */
/* -------------------------------------------------------------------------------- */
static int icx_key_open(struct input_dev *dev) 
{
  TRACE("dev=%p\n", dev);
  return 0;
}

/* -------------------------------------------------------------------------------- */
/*  input device close                                                              */
/* -------------------------------------------------------------------------------- */
static void icx_key_close(struct input_dev *dev) 
{
  TRACE("dev=%p\n", dev);
}

/* -------------------------------------------------------------------------------- */
/*  platform device prove                                                           */
/* -------------------------------------------------------------------------------- */
static int icx_key_probe(struct platform_device *pdev)
{
  int i;
  int ret;
  int ch;
  icx_key_dev_t *dev = &icx_key_dev;
  struct device *p_dev ;
  int result ;
  
  TRACE("icx_key: prove");
  
  INIT_DELAYED_WORK(&icx_key_dev.dwork, workq_func);
  INIT_DELAYED_WORK(&icx_key_dev.dwork_chat, workq_func);
  
  /* allocate input_dev and initialize it. */
  icx_key_dev.dev = input_allocate_device();
  if (!icx_key_dev.dev) {
    ERROR("input_allocate_device() failed\n");
    return -ENOMEM;
  }
  
  icx_key_dev.dev->name = ICX_KEY_NAME;
  icx_key_dev.dev->open = icx_key_open;
  icx_key_dev.dev->close = icx_key_close;
  
  set_bit(EV_KEY, icx_key_dev.dev->evbit);
  
  for (ch = 0; ch < NUM_KEY_AD_CH; ch++) {
    for (i = 0; i < NUM_KEY_INDEX; i++) {
      set_bit(icx_key_code[ch][i], icx_key_dev.dev->keybit);
    }
  }
  set_bit(KC_HOLD, icx_key_dev.dev->keybit);
  set_bit(KC_NC, icx_key_dev.dev->keybit);
  set_bit(KC_WAKEUP, icx_key_dev.dev->keybit);
  
  /* initialize key_state */
  reset_key_state();
  
  dev->suspend_flag = SUSPEND_FLAG_DISABLED;
  atomic_set(&dev->busy_flag, BUSY_FLAG_DISABLED);
  atomic_set(&dev->poll_flag, POLL_FLAG_DISABLED);
  
  atomic_set(&icx_key_dev.workq_flag, WORKQ_FLAG_ENABLED);
  
  /* KEY_WAKE */
#ifdef CONFIG_ENABLE_P4BB_KEY
  mt_eint_registration(eint_num_key_wake, EINTF_TRIGGER_HIGH, key_xPower_irq_handler, 1);
  mt_eint_dis_debounce(eint_num_key_wake);
#else
  #ifndef KEY_POLLING_DETECTION
  TRACE("*****eint_num_key_wake = %d\n", eint_num_key_wake);
  mt_eint_registration(eint_num_key_wake, EINTF_TRIGGER_RISING, key_wake_irq_handler, 1);
  mt_eint_dis_debounce(eint_num_key_wake);
  #endif // KEY_POLLING_DETECTION
#endif
  
#ifndef CONFIG_ENABLE_P4BB_KEY
  /* HOLD key*/
#ifndef CONFIG_DISABLE_HOLD_KEY
  mt_set_gpio_mode(gpio_num_hold, GPIO_MODE_00);
  mt_set_gpio_dir(gpio_num_hold, GPIO_DIR_IN);
  mt_set_gpio_pull_enable(gpio_num_hold, GPIO_PULL_DISABLE);
#endif
  
  TRACE("*****gpio eint_num_hold = %d\n", eint_num_hold);
#ifdef CONFIG_DISABLE_HOLD_KEY
  input_event(dev->dev, EV_KEY, KC_HOLD, INPUT_EVENT_UP);
#else
  mt_eint_registration(eint_num_hold, EINTF_TRIGGER_HIGH, key_xhold_irq_handler, 1);
  input_event(dev->dev, EV_KEY, KC_HOLD, INPUT_EVENT_DOWN);
#endif
  input_sync( dev->dev );
#endif
  
  if (eint_num_key_wake == EINT_NUM_7) { // for only PF DTB
    /* NC key*/
    if ((ret = mt_set_gpio_mode(gpio_num_nc, GPIO_MODE_00)) != RSUCCESS ) {
      ERROR("*****Failed to set gpio mode for NC\n");
      return ret;
    }
    if ((ret = mt_set_gpio_dir(gpio_num_nc, GPIO_DIR_IN)) != RSUCCESS ) {
      ERROR("*****Failed to set gpio dir for NC\n");
      return ret;
    }
    if ((ret = mt_set_gpio_pull_enable(gpio_num_nc, GPIO_PULL_DISABLE)) != RSUCCESS ) {
      ERROR("*****Failed to set gpio pull enable for NC\n");
      return ret;
    }
    
    TRACE("*****gpio eint_num_nc = %d\n", eint_num_nc);
    mt_eint_registration(eint_num_nc, EINTF_TRIGGER_HIGH, key_nc_irq_handler, 1);
    input_event(dev->dev, EV_KEY, KC_NC, INPUT_EVENT_DOWN);
    input_sync( dev->dev );
  }
  
  /* register device with input core */
  if ((ret = input_register_device(icx_key_dev.dev)) != 0) {
    ERROR("input_register_device() failed: ret=%d\n", ret);
    goto input_register_device_failure;
  }
  
  
  /* get a major number */
  result = alloc_chrdev_region(&m_devid, 0, 1, DRV_NAME) ;
  if( result ) {
    ERROR("\e[31m******** error %s(%d) : %d\e[0m\n", __func__, __LINE__, result) ;
    return result ;
  }
  TRACE("major = %d\n", MAJOR( m_devid )) ;
  
  /* initialize a cdev structure */
  cdev_init(&m_cdev, &icx_key_fops) 	;
  m_cdev.owner = THIS_MODULE ;
  /* add a character device to the system */
  result       = cdev_add(&m_cdev, m_devid, 1) ;
  if( result ) {
    ERROR("\e[31m******** error %s(%d) : %d\e[0m\n", __func__, __LINE__, result) ;
    goto cdev_module_init_f00 ;
  }
  
  /* create a class structure */
  m_p_class = class_create(THIS_MODULE, DRV_NAME) ;
  if( IS_ERR( m_p_class ) ) {
    ERROR("\e[31m******** error %s(%d) : %ld\e[0m\n", __func__, __LINE__, PTR_ERR( m_p_class )) ;
    goto cdev_module_init_f10 ;
  }
  
  /* create a device file */
  p_dev = device_create(m_p_class, NULL, m_devid, NULL, DRV_NAME) ;
  if( IS_ERR( p_dev ) ) {
    ERROR("\e[31m******** error %s(%d) : %ld\e[0m\n", __func__, __LINE__, PTR_ERR( p_dev )) ;
    goto cdev_module_init_f20 ;
  }

#ifdef DPIDLE_CAN_ENTER_EXTRA_STATUS
  register_dpidle_key_status_func(cdev_icx_key_is_active);
#endif
  
  /* going to scan ad_key right away. */
  if (atomic_read(&icx_key_dev.workq_flag) == WORKQ_FLAG_ENABLED) {
    schedule_delayed_work(&icx_key_dev.dwork, 0);
  }
  
  return 0;
  
input_register_device_failure:
  return ret;
  
cdev_module_init_f20:
  class_destroy( m_p_class ) ;

cdev_module_init_f10:
  cdev_del( &m_cdev ) ;

cdev_module_init_f00:
  unregister_chrdev_region(m_devid, 1) ;
  
  return 0 ;

}

/* -------------------------------------------------------------------------------- */
/*  platform device remove                                                          */
/* -------------------------------------------------------------------------------- */
static int icx_key_remove(struct platform_device *pdev)
{
  TRACE("icx_key: remove");
  
  icx_key_end_processing();
  
  return 0;
}

/* -------------------------------------------------------------------------------- */
/*  platform device shutdown                                                        */
/* -------------------------------------------------------------------------------- */
static void icx_key_shutdown(struct platform_device *dev)
{
  TRACE("icx_key: shutdown\n");
  
  icx_key_end_processing();
}

/* -------------------------------------------------------------------------------- */
/*  platform device suspend                                                         */
/* -------------------------------------------------------------------------------- */
static int icx_key_suspend(struct platform_device *dev, pm_message_t state)
{
  TRACE("icx_key: suspend");
  
  icx_key_dev.suspend_flag = SUSPEND_FLAG_ENABLED;
  
  return 0;
}

/* -------------------------------------------------------------------------------- */
/*  platform device resume                                                          */
/* -------------------------------------------------------------------------------- */
static int icx_key_resume(struct platform_device *dev)
{
  TRACE("icx_key: resume");
  
  if (icx_key_dev.suspend_flag == SUSPEND_FLAG_ENABLED) {
    icx_key_dev.suspend_flag = SUSPEND_FLAG_DISABLED;
  }

#ifdef CONFIG_ENABLE_P4BB_KEY
  schedule_delayed_work(&icx_key_dev.dwork, 0);
#endif
  
  return 0;
}

static struct platform_driver icx_key_driver = {
    .probe    = icx_key_probe,
    .remove   = icx_key_remove,
    .shutdown = icx_key_shutdown,
    .suspend  = icx_key_suspend,
    .resume   = icx_key_resume,
    .driver   = {
    .name     = ICX_KEY_NAME,
    },
};

static int __init icx_key_init(void)
{
  TRACE("icx_key: init");
  
  platform_driver_register(&icx_key_driver);
  
  return 0 ;
}

static void __exit icx_key_exit(void)
{
  TRACE("icx_key: exit");
  
  platform_driver_unregister(&icx_key_driver);
}

module_init(icx_key_init);
module_exit(icx_key_exit);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("Sony ICX Key Driver");
MODULE_LICENSE("GPL");

