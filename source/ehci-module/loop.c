/*   
	Custom IOS Module (EHCI)

	Copyright (C) 2008 neimod.
	Copyright (C) 2009 kwiirk.
	Copyright (C) 2009 Hermes.
	Copyright (C) 2009 Waninkoko.
	Copyright (C) 2011 davebaol.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ehci_config.h"
#include "ehci_irq.h"
#include "ehci_types.h"
#include "ehci.h"
#include "ipc.h"
#include "mem.h"
#include "module.h"
#include "sdhc_server.h"
#include "stealth.h"
#include "syscalls.h"
#include "usbstorage.h"
#include "utils.h"
#include "watchdog.h"
#include "wbfs_usb.h"


/* Handlers */
static u32 queuehandle;

/* Variables */
static u32 discovered = 0;
static u32 ums_mode   = 0;


static char *__EHCI_ParseHex(char *base, s32 *val)
{
	s32 v = 0;

	char *ptr = base, c;

	while (1) {
		c = *ptr++;

		if(c >= '0' &&  c <= '9')
			v = v << 4 | (c-'0');
		else if(c >= 'a' &&  c <= 'f')
			v = v << 4 | (10+c-'a');
		else if(c >= 'A' &&  c <= 'F')
			v = v << 4 | (10+c-'A');
		else
			break;
	}

	if (ptr == base + 1)
		return 0;

	*val = v;

	return (ptr - 1);
}

static s32 __EHCI_Ioctlv(s32 fd, u32 cmd, ioctlv *vector, u32 inlen, u32 iolen, s32 *ack)
{
	s32 ret = 0;

	/* Convert FD to device */
	void *dev = ehci_fd_to_dev(fd);

	/* Invalidate cache */
	InvalidateVector(vector, inlen, iolen);

	/* Parse IOCTLV command */
	switch (cmd) {
	case USB_IOCTL_CTRLMSG: {
		u8    type    =  *(u8 *)vector[0].data;
		u8    request =  *(u8 *)vector[1].data;
		u16   wValue  = *(u16 *)vector[2].data;
		u16   wIndex  = *(u16 *)vector[3].data;
		u16   wLength = *(u16 *)vector[4].data;

		void *buffer  = vector[6].data;

		if (dev)
			ret = ehci_control_message(dev, type, request, swab16(wValue),swab16(wIndex), swab16(wLength), buffer);
		else
			ret = IPC_ENOENT;

		break;
	}

	case USB_IOCTL_BLKMSG: {
		u8  endpoint =  *(u8 *)vector[0].data;
		u16 wLength  = *(u16 *)vector[1].data;

		void *buffer = vector[2].data;

		if (dev)
			ret = ehci_bulk_message(dev, endpoint, wLength, buffer);
		else
			ret = IPC_ENOENT;

		break;
	}

	case USB_IOCTL_GETDEVLIST: {
		u8  maxdev = *(u8 *)vector[0].data;
		u8  b0     = *(u8 *)vector[1].data;
		u8 *num    =  (u8 *)vector[2].data;

		void *buffer = vector[3].data;

		if (!dev)
			ret = ehci_get_device_list(maxdev, b0, num, buffer);
		else
			ret = IPC_ENOENT;

		break;
	}

	case USB_IOCTL_DEVREMOVALHOOK:
	case USB_IOCTL_DEVINSERTHOOK: {
		/* Do not reply */
		*ack = 0;

		break;
	}

	case USB_IOCTL_UMS_INIT: {
		/* Initialize USB storage */
		ret = USBStorage_Init();

		/* Set UMS mode */
		ums_mode = fd;

		break;
	}

	case USB_IOCTL_UMS_UMOUNT: {
		/* Umount USB storage */
		USBStorage_Umount();

		break;
	}

	case USB_IOCTL_UMS_GET_CAPACITY: {
		void *buffer = vector[0].data;

		/* Get capacity */
		ret = USBStorage_Get_Capacity(buffer);

		break;
	}

	case USB_IOCTL_UMS_READ_SECTORS: {
		void *buffer = vector[2].data;

		u32   lba    = *(u32 *)vector[0].data;
		u32   count  = *(u32 *)vector[1].data;

		/* Read sectors */
		ret = !USBStorage_Read_Sectors(lba, count, buffer);

		break;
	}

	case USB_IOCTL_UMS_WRITE_SECTORS: {
		void *buffer = vector[2].data;

		u32   lba    = *(u32 *)vector[0].data;
		u32   count  = *(u32 *)vector[1].data;

		/* Write sectors */
		ret = !USBStorage_Write_Sectors(lba, count, buffer);

		break;
	}

	case USB_IOCTL_UMS_READ_STRESS: {
#if 0
		void *buffer = vector[2].data;

		u32   lba    = *(u32 *)vector[0].data;
		u32   count  = *(u32 *)vector[1].data;

		/* Read stress */
		ret = !USBStorage_Read_Stress(lba, count, buffer);
#endif

		break;
	}

	case USB_IOCTL_UMS_SET_VERBOSE: {
		/* Do nothing */
		break;
	}

	case USB_IOCTL_UMS_WATCHDOG: {
		u32 secs = *(u32 *)vector[0].data;

		/* Set watchdog timeout */
		config.watchdogTimeout = secs * USECS_PER_SEC;

		break;
	}

	case USB_IOCTL_UMS_SAVE_CONFIG: {

		/* Save EHCI config */
		EHCI_SaveConfig();

		break;
	}

	case USB_IOCTL_WBFS_OPEN_DISC: {
		u8 *discid = (u8 *)vector[0].data;

		/* Open WBFS disc */
		ret = WBFS_USB_OpenDisc(discid);

		break;
	}

	case USB_IOCTL_WBFS_READ_DISC: {
		void *buffer = vector[2].data;

		u32   offset = *(u32 *)vector[0].data;
		u32   len    = *(u32 *)vector[1].data;

		/* Read WBFS disc */
		ret = WBFS_USB_Read(buffer, len, offset);

		break;
	}

	default:
		/* Unknown command */
		ret = IPC_EINVAL;
	}

	/* Flush cache */
	FlushVector(vector, inlen, iolen);

	return ret;
}

static s32 __EHCI_OpenDevice(char *devname, s32 fd)
{
	char *ptr = devname;

	s32 vid, pid;

	/* Parse vendor ID */
	ptr = __EHCI_ParseHex(ptr, &vid);
	if (!ptr)
		return -6;

	if (*ptr != '/')
		return -6;

	ptr++;

	/* Parse product ID */
	ptr = __EHCI_ParseHex(ptr, &pid);
	if (!ptr)
		return -6;

	if (*ptr != '\0')
		return -6;

	/* Open device */
	return ehci_open_device(vid, pid, fd);
}


static s32 __EHCI_Init(u32 *queuehandle)
{
	void *buffer = NULL;
	s32   ret;

	/* Allocate buffer*/
	buffer = Mem_Alloc(0x80);
	if (!buffer)
		return IPC_ENOMEM;

	/* Create message queue */
	ret = os_message_queue_create(buffer, 32);
	if (ret < 0)
		return ret;

	/* Set queue handler */
	*queuehandle = ret;
	
	/* Initialize interrupts */
	ehci_irq_init();

	/* Set thread priority */
	os_thread_set_priority(os_get_thread_id(), 0x78);

	/* Register USB device */
	os_device_register(DEVICE_USB_NO_SLASH, *queuehandle);
	os_device_register(DEVICE_USB, *queuehandle);

	/* Register SDHC device */
	SDHC_RegisterDevice(*queuehandle);

	/* Create watchdog timer */
	ret = WATCHDOG_CreateTimer(*queuehandle, 0);

	return ret;
}


s32 EHCI_Loop(void)
{
	s32 ret;

	/* Initialize module */
	ret = __EHCI_Init(&queuehandle);
	if (ret < 0)
		return ret;

	/* Main loop */
	while (1) {
		ipcmessage *message = NULL;
		s32 ack = 1;

		/* Wait for message */
		ret = os_message_queue_receive(queuehandle, (void *)&message, 0);
		if (ret)
			continue;

		/* Stop watchdog timer */
		WATCHDOG_StopTimer();

		/* Watchdog message */
		if (!message) {
			/* UMS mode */
			if (ums_mode) {
				/* Run watchdog */
				WATCHDOG_Run();

				/* Restart timer */
				WATCHDOG_RestartTimer();
			}
			continue;
		}

		/* IPC command */
		switch(message->command) {
		case IOS_OPEN: {
			/* Block opening request not coming from ES when a title is running */
			ret = Swi_GetRunningTitle() && !Swi_GetEsRequest();
			if (ret) {
				Stealth_Log(STEALTH_RUNNING_TITLE | STEALTH_ES_REQUEST, NULL);
				ret = IPC_ENOENT;
				break;
			}

			char *devpath  = message->open.device;
			s32   resultfd = message->open.resultfd;

			/* SDHC module device */
			if (SDHC_CheckDevicePath(devpath)) {
				ret = SDHC_FD;
				break;
			}

			/* USB module device */
			if (!strcmp(devpath, DEVICE_USB) || !strcmp(devpath, DEVICE_USB_NO_SLASH)) {
				/* Discover devices */
				if (!discovered)
					ehci_discover();

				/* Set discovered flag */
				discovered = 1;

				ret = resultfd;

				break;
			}

			/* USB device */
			if (!ums_mode) {
				/* USB device specified through standard module name */
				if (!strncmp(devpath, DEVICE_USB "/", sizeof(DEVICE_USB))) {
					/* Open USB device */
					ret = __EHCI_OpenDevice(devpath + sizeof(DEVICE_USB), resultfd);

					break;
				}

				/* USB device specified through module name whit no initial slash  */
				if (!strncmp(devpath, DEVICE_USB_NO_SLASH "/", sizeof(DEVICE_USB_NO_SLASH))) {
					/* Open USB device */
					ret = __EHCI_OpenDevice(devpath + sizeof(DEVICE_USB_NO_SLASH), resultfd);

					break;
				}
			}

			/* Wrong device */
			ret = IPC_ENOENT;

			break;
		}

		case IOS_CLOSE: {
			/* Close SDHC device */
			if(message->fd == SDHC_FD) {
				ret = SDHC_Close();
				break;
			}

			/* Close USB device */
			if(ums_mode != message->fd)
				ehci_close_device(ehci_fd_to_dev(message->fd));
			else
				ums_mode = 0;

			/* Success */
			ret = 0;

			break;
		}

		case IOS_IOCTLV: {
			ioctlv *vector = message->ioctlv.vector;
			u32     inlen  = message->ioctlv.num_in;
			u32     iolen  = message->ioctlv.num_io;
			u32     cmd    = message->ioctlv.command;

			/* Parse IOCTLV command */
			if (message->fd == SDHC_FD)
				ret = SDHC_Ioctlv(cmd, vector, inlen, iolen);
			else
				ret = __EHCI_Ioctlv(message->fd, cmd, vector, inlen, iolen, &ack);

			break;
		}

		default:
			/* Unknown command */
			ret = IPC_EINVAL;
		}

		/* Restart watchdog timer */
		WATCHDOG_RestartTimer();

		/* Acknowledge message */
		if (ack)
			os_message_queue_ack(message, ret);
	}
   
	return 0;
}