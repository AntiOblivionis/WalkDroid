/* 2013-07-16: File added by Sony Corporation */
/*
 *  icx_key.h
 *
 *  Copyright 2009 Sony Corporation
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  version 2 of the  License.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA.
 */

#ifndef _ICX_KEY_H_
#define _ICX_KEY_H_

#include <linux/types.h>

/* AD KEY */
/* AD0 */
#define KEY_ENTER                      28
#define KEY_RIGHT                      106
#define KEY_LEFT                       105
#define KEY_UP                         103
#define KEY_DOWN                       108
#define KEY_HOME                       102
#define KEY_F6                         64
/* AD1 */
#define KEY_VOLUMEDOWN                 114
#define KEY_VOLUMEUP                   115
#define KEY_BACK                       158
#define KEY_MENU                       139
#define KEY_BLUETOOTH                  237
#define KEY_F15                        185
#define KEY_F16                        186

/* GPIO KEY */
#define KEY_H                          35
#define KEY_N                          49
#define KEY_F17                        187
#define KEY_POWER                      116

/* resume */
#define KEY_WAKEUP                     143

#define KEY_RESERVED                   0

/* Key value */
#define ICX_KEY_VALUE_UP               ((__s32)0)
#define ICX_KEY_VALUE_DOWN             ((__s32)1)

/* ioctl */
#define ICX_KEY_IOCTL_IOC_MAGIC                'p'
#define ICX_KEY_IOCTL_CMD_GETCOUNT             _IO(ICX_KEY_IOCTL_IOC_MAGIC, 0)
#define ICX_KEY_IOCTL_CMD_GETINFO              _IOR(ICX_KEY_IOCTL_IOC_MAGIC, 2, uint32_t)
/* for getting AD key code that is used for resume */ 
#define ICX_KEY_IOCTL_CMD_GET_RESUME_ADKEY     _IOR(ICX_KEY_IOCTL_IOC_MAGIC, 3, uint32_t)
/* for getting GPIO key code that is used for resume */ 
#define ICX_KEY_IOCTL_CMD_GET_RESUME_GPIOKEY   _IOR(ICX_KEY_IOCTL_IOC_MAGIC, 4, uint32_t)
/* for getting key code (AD key or GPIO key) that is used for resume */ 
#define ICX_KEY_IOCTL_CMD_GET_RESUME_KEY       _IOR(ICX_KEY_IOCTL_IOC_MAGIC, 5, uint32_t)

#endif /* _ICX_KEY_H_ */

