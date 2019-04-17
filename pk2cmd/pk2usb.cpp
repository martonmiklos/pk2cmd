//
// USB interface for Linux
//
// By Jeff Post, j_post <AT> pacbell <DOT> net
// To contact the author, please include the word "pk2cmd" in your subject line,
//  otherwise your email may be rejected as spam.
//
//                            Software License Agreement
//
// You may use, copy, modify and distribute the Software for use with Microchip
// products only.  If you distribute the Software or its derivatives, the
// Software must have this entire copyright and disclaimer notice prominently
// posted in a location where end users will see it (e.g., installation program,
// program headers, About Box, etc.).  To the maximum extent permitted by law,
// this Software is distributed "AS IS" and WITHOUT ANY WARRANTY INCLUDING BUT
// NOT LIMITED TO ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR
// PARTICULAR PURPOSE, or NON-INFRINGEMENT. IN NO EVENT WILL MICROCHIP OR ITS
// LICENSORS BE LIABLE FOR ANY INCIDENTAL, SPECIAL, INDIRECT OR CONSEQUENTIAL
// DAMAGES OF ANY KIND ARISING FROM OR RELATED TO THE USE, MODIFICATION OR
// DISTRIBUTION OF THIS SOFTWARE OR ITS DERIVATIVES.
//
//---------------------------------------------------------------------------
#ifndef	WIN32		// This module is not used by the Windows build

// Comment out the following line if you do not use usb hotplug
#define	USB_HOTPLUG

#include	<stdio.h>
#include <usb.h>			// libusb header
#include <unistd.h>		// for geteuid
#include	<ctype.h>
#include	<string.h>

#include	"stdafx.h"
#include	"pk2usb.h"

#if HAVE_LIBUSB_INTERRUPT_MODE
// latest libusb: interrupt mode, works with all kernels
#  define PICKIT_USB(direction) usb_interrupt_##direction
#else 
// older libusb: bulk mode, will only work with older kernels
#  define PICKIT_USB(direction) usb_bulk_##direction
#endif

// Prototypes
//   none

// Data

PickitType_t deviceType = Pickit2;
usb_dev_handle	*deviceHandle = NULL;

// PICkit USB values

const static int pickit_vendorID = 0x04d8;	// Microchip, Inc
const static int pickit2_productID = 0x0033;	// PICkit 2 FLASH starter kit
const static int pickit3_productID = 0x900a;	// PICkit 3

const static int pickit_endpoint_out = 1;		// endpoint 1 address for OUT
const static int pickit_endpoint_in = 0x81;	// endpoint 0x81 address for IN
const static int pickit_timeout = 10000;		// timeout in ms

// Code

// Send 64 byte block to PICkit2

int sendUSB(pickit_dev *d, byte *src, int len)
{
	int	r, i;
	bool	rescan = false;

    if (pickit_mode == NORMAL_MODE)
	{
		if ((src[0] == ENTERBOOTLOADER) && (src[1] == END_OF_BFR))
			rescan = true;
	}
	else
	{
		if (src[0] == RESETFWDEVICE)
			rescan = true;
	}

	if (usbdebug & USB_DEBUG_CMD)
		showUSBCommand(src, len);

	if (usbdebug & USB_DEBUG_XMIT)
	{
		if (!usbFile)
			usbFile = stdout;

		fprintf(usbFile, "USB>");

		for (r=0, i=0; r<len; r++)
		{
			fprintf(usbFile, " %02x", src[r] & 0xff);
			i++;

			if ((i > 15) && (i < (len - 1)))
			{
				i = 0;
				fprintf(usbFile, "\n    ");
			}
		}

		fprintf(usbFile, "\n");
		fflush(usbFile);
	}

	r = PICKIT_USB(write)(d, pickit_endpoint_out, (char *) src, reqLen, pickit_timeout);

	if (rescan)		// Microchip code entered/exited bootloader,
		deviceHandle = NULL;	// so reset the device handle

	if (r != reqLen)
	{
		if (verbose)
		{
			printf("sendUSB() PICkit USB write failed, returned %d\n", r);
			fflush(stdout);
		}

		return 0;
	}

	return 1;
}

// A timeout in the driver read routine does not return 0 bytes, as it should.
// We need some way to detect timeouts so that higher level routines will know.

int readBlock(pickit_dev *d, int len, byte *dest)
{
	int	i, j, r;
	byte	bfr[reqLen + 1];

	r = PICKIT_USB(read)(d, pickit_endpoint_in, (char *) bfr, reqLen, pickit_timeout);

	if (r != reqLen)
	{
		if (verbose)
		{
			printf("USB read did not return 64 bytes\n");
			fflush(stdout);
		}

		return 0;
	}

	if (usbdebug & USB_DEBUG_RECV)
	{
		if (!usbFile)
			usbFile = stdout;

		fprintf(usbFile, "USB<");

		for (i=0, j=0; i<len; i++)
		{
			fprintf(usbFile, " %02x", bfr[i] & 0xff);
			j++;

			if ((j > 15) && (i < (len - 1)))
			{
				j = 0;
				fprintf(usbFile, "\n    ");
			}
		}

		fprintf(usbFile, "\n");
		fflush(usbFile);
	}

	for (i=0; i<len; i++)
		dest[i] = bfr[i];

	return len;
}

// Read this many bytes from this device

int recvUSB(pickit_dev *d, int len, byte *dest)
{
	int r = readBlock(d, len, dest);

	if (r != len)
	{
		if (verbose)
		{
			printf("recvUSB() PICkit USB read failed\n");
			fflush(stdout);
		}

		return 0;
	}

	return 1;
}

// debugging: enable debugging error messages in libusb

// Find the given USB device with this vendor and product.
// Returns NULL on errors, like if the device couldn't be found.

pickit_dev *usbPickitOpen(int unitIndex, char *unitID)
{
	int					result, unitNumber;
	struct usb_device	*device;
	struct usb_device	*usb_devices;
	struct usb_bus		*bus;
	usb_dev_handle		*d = NULL;
    char                unitIDSerial[64];
    byte				retData[reqLen + 1];

#ifdef LINUX
	int					retval;
	char					dname[32] = {0};
#ifndef USE_DETACH
	char					syscmd[64];
#endif
#endif

#ifndef USB_HOTPLUG
	if (geteuid() != 0)
		return NULL;
#endif

#ifdef LINUX
	if (usbdebug & USB_DEBUG_DRIVER)
		usb_set_debug(255);
#endif

	if (verbose)
	{
        printf("\nLocating USB Microchip PICkit2/3 (0x%04x:0x%04x/0x%04x:0x%04x)\n",
            pickit_vendorID, pickit2_productID,
            pickit_vendorID, pickit3_productID);
		fflush(stdout);
	}

	usb_init();
	result = usb_find_busses();

	if ((result < 0) && verbose)
	{
		printf("Error finding USB busses: %d\n", result);
		fflush(stdout);
	}

	result = usb_find_devices();

	if ((result < 0) && verbose)
	{
		printf("Error finding USB devices: %d\n", result);
		fflush(stdout);
	}

	unitNumber = 0;

	for (bus = usb_get_busses(); bus != NULL; bus = bus->next)
	{
		usb_devices = bus->devices;

		for (device = usb_devices; device != NULL; device = device->next)
		{
			if (device->descriptor.idVendor == pickit_vendorID
                && (device->descriptor.idProduct == pickit2_productID
                    || device->descriptor.idProduct == pickit3_productID))
			{
				if (unitIndex == unitNumber)
				{
					if (verbose)
					{
                        printf( "Found PICkit%d as device '%s' on USB bus %s\n",
                            device->descriptor.idProduct == pickit2_productID ? 2 : 3,
							device->filename,
							device->bus->dirname);
						fflush(stdout);
					}

					unitID[0] = '-';
					unitID[1] = 0;

                    d = usb_open(device);
					deviceHandle = d;

					if (device->descriptor.iSerialNumber > 0)
					{
						usb_get_string_simple(d, device->descriptor.iSerialNumber, unitIDSerial, 64);

						if (unitIDSerial[0] && (unitIDSerial[0] != 9))
						{
							strcpy(unitID, unitIDSerial);
							unitID[14] = 0; // ensure termination after 14 characters
						}
					}

					if (d)
					{			// This is our device
	#ifdef LINUX
						{
							retval = usb_get_driver_np(d, 0, dname, 31);
	#ifndef USE_DETACH
							if (!retval)
							{
								strcpy(syscmd, "rmmod ");
								strcat(syscmd, dname);
	//			printf("removing driver module: %s\n", syscmd);
								system(syscmd);
							}
	#else
							if (!retval)
								usb_detach_kernel_driver_np(d, 0);
	#endif
						}
	#endif

	#ifdef CLAIM_USB

						if (usb_set_configuration(d, CONFIG_VENDOR) < 0)	// if config fails with CONFIG_VENDOR,
						{
							if (usb_set_configuration(d, CONFIG_HID) < 0)	// it may be in bootloader, try CONFIG_HID
							{
								if (verbose)
								{
									printf("Error setting USB configuration.\n");
									fflush(stdout);
								}

								return NULL;
							}
						}

						if (usb_claim_interface(d, pickit_interface))
						{
							if (verbose)
							{
								printf("Claim failed-- the USB PICkit2 is in use by another driver.\n"
									"Do a `dmesg` to see which kernel driver has claimed it--\n"
									"You may need to `rmmod hid` or patch your kernel's hid driver.\n");
								fflush(stdout);
							}

							return NULL;
						}
	#endif

                        if (device->descriptor.idProduct == pickit2_productID)
                        {
                            deviceType = Pickit2;
                            cmd[0] = GETVERSION;
                            sendPickitCmd(deviceHandle, cmd, 1);
                            recvUSB(deviceHandle, 8, retData);

                            if (retData[5] == 'B')
                            {
                                if (verbose)
                                {
                                    printf("Communication established. PICkit2 bootloader firmware version is %d.%d\n\n",
                                        (int) retData[6], (int) retData[7]);
                                    fflush(stdout);
                                }

                                pickit_mode = BOOTLOAD_MODE;
                                pickit_firmware = (((int) retData[6]) << 8) | (((int) retData[7]) & 0xff);
                            }
                            else
                            {
                                if (verbose)
                                {
                                    printf("Communication established. PICkit2 firmware version is %d.%d.%d\n",
                                        (int) retData[0], (int) retData[1], (int) retData[2]);
                                    fflush(stdout);
                                }

                                pickit_mode = NORMAL_MODE;
                                pickit_firmware = (((int) retData[0]) << 16) | ((((int) retData[1]) << 8) & 0xff00) | (((int) retData[2]) & 0xff);
                            }
                        }
                        else
                        {
                            deviceType = Pickit3;
                            cmd[0] = CMD_GETVERSIONS_MPLAB;
                            cmd[1] = 0;

                            memset(cmd, 0xAD, 64);
                            cmd[0] = CMD_GETVERSIONS_MPLAB;
                            cmd[1] = 0;

                            cmd[60] = 2;
                            cmd[61] = 0;
                            cmd[62] = 0;
                            cmd[63] = 0;
                            sendPickitCmd(deviceHandle, cmd, 64);
                            recvUSB(deviceHandle, 64, retData);

                            if (retData[30] != 'P' ||
                                retData[31] != 'k' ||
                                retData[32] != '3')
                            {
                                printf("Incompatible PICkit3 firmware detected\n");
                                printf("Please, upgrade the firmware using PICkit 3 Scripting Tool.\n");
                                fflush(stdout);
                                usb_close(deviceHandle);
                                return NULL;
                            }
                            else
                            {
                                if (verbose)
                                {
                                    printf("Communication established. PICkit3 scripting firmware version is %d.%d.%d\n",
                                        (int) retData[33], (int) retData[34], (int) retData[35]);
                                    fflush(stdout);
                                }

                                pickit_mode = NORMAL_MODE;
                                pickit_firmware = (((int) retData[33]) << 16) | ((((int) retData[34]) << 8) & 0xff00) | (((int) retData[35]) & 0xff);
                            }
                        }

                        return d;
					}
					else 
					{
						if (verbose)
						{
							printf("Open failed for USB device\n");
							fflush(stdout);
						}

						return NULL;
					}
				}
				else
				{ // not the unit we're looking for
					unitNumber++;
				}
			}
			// else some other vendor's device -- keep looking...
		}
	}

	if (verbose)
	{
        printf("Could not find any PICkit2/PICkit3 (with scripting mode firmware) programmer--\n"
			"you might try lsusb to see if it's actually there.\n");
		fflush(stdout);
	}
    return deviceHandle;
}

#endif	// #ifndef	WIN32

// end
