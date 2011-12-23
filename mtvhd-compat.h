
#ifndef __MTVHD_COMPAT_H__
#define __MTVHD_COMPAT_H__

#include <linux/version.h>
#include <linux/input.h>
//#include "config-compat.h"
//#include "../linux/kernel_version.h"

// CONFIG STUFF

//#define CONFIG_MEDIA_SUPPORT_MODULE 1
//#define CONFIG_DVB_CORE_MODULE 1
//#define CONFIG_DVB_USB_MODULE 1
//#define CONFIG_DVB_USB_DEBUG 1

#define CONFIG_DVB_USB_MTVHD_MODULE 1
#define CONFIG_DVB_USB_MTVHD_V2 1
#define CONFIG_DVB_USB_MTVHD_V1 1

#define CONFIG_DVB_USB_ASV5211_MODULE 1
#define CONFIG_DVB_USB_ASV5211_WIN_DRIVER 1

#define CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL 1

#endif // __MTVHD_COMPAT_H__
