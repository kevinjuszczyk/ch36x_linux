/*
 * ch365/ch367/ch368 PCI/PCIE driver - Copyright (C) 2021 WCH Corporation.
 * Author: TECH39 <zhangj@wch.cn>
 *
 * Sponsored by SuSE
 *
 * System required:
 * Kernel version beyond 2.6.x
 * Update Log:
 * V1.0 - initial version
 * V1.1 - modified io read/write methods with standard api
 *		   not pointer access.
 * V1.2 - modified io/mem mapping, fixed usage of interruption.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#define DEBUG
#define VERBOSE_DEBUG

#undef DEBUG
#undef VERBOSE_DEBUG

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/cdev.h>		
#include <linux/slab.h>		
#include <linux/types.h>	
#include <linux/fs.h>	
#include <linux/device.h>	
#include <linux/fcntl.h>
#include <linux/ioport.h>

#include <linux/uaccess.h>	
#include <asm/io.h>			
#include <linux/string.h>
#include <linux/list.h>		
#include <linux/mutex.h>	
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/signal.h>		
#include <linux/wait.h>
#include <linux/sched.h>
                      
#define DRIVER_AUTHOR 			"TECH39 <zhangj@wch.cn>"
#define DRIVER_DESC 			"PCI/PCIE driver for chip ch365/ch367/ch368, etc."
#define VERSION_DESC  			"V1.2 On 2021.01.27"

#define CH36X_MAX_NUM			16
#define CH36X_DRV_NAME			"ch36xpci"

#define CH365_VID				0x4348	//Vendor id
#define CH365_DID				0x5049	//Device id
#define CH367_VID				0x1C00	//Vendor id
#define CH367_DID_SID_HIGH		0x5831	//Device id when SID high
#define CH367_DID_SID_LOW		0x5830	//Device id when SID low
#define CH367_SUB_VID			0x1C00	//Subsystem vendor id
#define CH367_SUB_DID_SID_HIGH	0x5831	//Subsystem Vendor id when SID high
#define CH367_SUB_DID_SID_LOW	0x5830	//Subsystem Device id when SID low
#define CH368_VID				0x1C00	//Vendor id
#define CH368_DID				0x5834	//Device id
#define CH368_SUB_VID			0x1C00	//Subsystem Vendor id
#define CH368_SUB_DID			0x5834	//Subsystem Device id

//size of read or write 
#define SIZE_BYTE					0x01
#define SIZE_WORD					0x02
#define SIZE_DWORD					0x03

#define TRIGGER_LOW					0xe0
#define TRIGGER_HIGH				0xe4
#define TRIGGER_RISING				0xe8
#define TRIGGER_FALLING				0xec

//address of cfg/IO/MEM register
#define OFFSET_IO_BASE_ADDR			0x10	// I/O Base Address
#define OFFSET_MEM_BASE_ADDR		0x14	// Memory Base Address	
#define OFFSET_INT_LINE				0x3C	// Interrupt Line & Pin

/* IOCTRL register bits */
#define CH365_IOCTRL_A15_BIT		BIT(0) /* Set A15 */
#define CH365_IOCTRL_SYS_EX_BIT		BIT(1) /* Set SYS_EX */
#define CH365_IOCTRL_INTA_BIT		BIT(2) /* INT Active status */


/* MICSR register bits */
#define CH367_MICSR_GPO_BIT			BIT(0) /* Set GPO */
#define CH367_MICSR_INTA_BIT		BIT(2) /* INT Active status */
#define CH367_MICSR_INTS_BIT		BIT(3) /* INT status */
#define CH367_MICSR_RSTO_BIT		BIT(7) /* Set RSTO */

/* INTCR register bits */
#define CH367_INTCR_MSI_ENABLE_BIT	BIT(0) /* MSI Enable */
#define CH367_INTCR_INT_ENABLE_BIT	BIT(1) /* Global INT Enable */
#define CH367_INTCR_INT_POLAR_BIT	BIT(2) /* Set INT Polar */
#define CH367_INTCR_INT_TYPE_BIT	BIT(3) /* Set INT Type */
#define CH367_INTCR_INT_RETRY_BIT	BIT(4) /* Set INT Retry */

#define CH365_STATUS_REG 0x42
#define CH365_STATUS_ENABLE_BIT		BIT(7) /* Global INT Enable */

enum CHIP_TYPE {
	CHIP_CH365 = 1,
	CHIP_CH367,
	CHIP_CH368
};

enum INTMODE {
	INT_NONE = 0,
	INT_LOW,
	INT_HIGH,
	INT_RISING,
	INT_FALLING
};

typedef	struct	_CH365_IO_REG {				// CH365芯片的I/O空间
	u8			mCh365IoPort[0xf0];			// 00H-EFH,共240字节为标准的I/O端口
	union	{								// 以字或者以字节为单位进行存取
		u16		mCh365MemAddr;				// F0H 存储器接口: A15-A0地址设定寄存器
		struct	{							// 以字节为单位进行存取
			u8	mCh365MemAddrL;				// F0H 存储器接口: A7-A0地址设定寄存器
			u8	mCh365MemAddrH;				// F1H 存储器接口: A15-A8地址设定寄存器
		};
	};
	u8			mCh365IoResv2;				// F2H
	u8			mCh365MemData;				// F3H 存储器接口: 存储器数据存取寄存器
	u8			mCh365I2cData;				// F4H I2C串行接口: I2C数据存取寄存器
	u8			mCh365I2cCtrl;				// F5H I2C串行接口: I2C控制和状态寄存器
	u8			mCh365I2cAddr;				// F6H I2C串行接口: I2C地址设定寄存器
	u8			mCh365I2cDev;				// F7H I2C串行接口: I2C设备地址和命令寄存器
	u8			mCh365IoCtrl;				// F8H 芯片控制寄存器,高5位只读
	u8			mCh365IoBuf;				// F9H 本地数据输入缓存寄存器
	u8			mCh365Speed;				// FAH 芯片速度控制寄存器
	u8			mCh365IoResv3;				// FBH
	u8			mCh365IoTime;				// FCH 硬件循环计数寄存器
	u8			mCh365IoResv4[3];			// FDH
} mCH365_IO_REG, *mPCH365_IO_REG;

typedef	struct	_CH365_MEM_REG {			// CH365芯片的存储器空间
	u8			mCh365MemPort[0x8000];		// 0000H-7FFFH,共32768字节为标准的存储器单元
} mCH365_MEM_REG, *mPCH365_MEM_REG;


typedef	struct	_CH367_IO_REG {	            //CH367芯片的I/O空间寄存器
	u8 mCH367IoPort[0xE8];                  //00H-E7H,共232字节为标准的I/O端口
	u8 mCH367GPOR;	                        //E8H 通用输出寄存器
	u8 mCH367GPVR;	                        //E9H 通用变量寄存器
	u8 mCH367GPIR;	                        //EAH 通用输入寄存器
	u8 mCH367IntCtr;	                    //EBH 中断控制寄存器
	union {
		u8 mCH367IoBuf8;                    //ECH 8位被动并行接口数据缓冲区
		u32 mCH367IoBuf32;					//ECH 32位被动并行接口数据缓冲区
	};
	union {
		u16 mCH368MemAddr;                  //F0H 存储器接口: A15-A0地址设定寄存器
		struct {
			u8 mCH368MemAddrL;              //F0H 存储器接口: A7-A0地址设定寄存器
			union {
				u8 mCH368MemAddrH;          //F1H 存储器接口: A15-A8地址设定寄存器
				u8 mCH367GPOR2;             //F1H 通用输出寄存器2
			};
		} ASR;
	};
	u8 mCH367IORESV2;                       //F2H
	u8 mCH368MemData;                       //F3H 存储器接口: 存储器数据存取寄存器
	union {
		u8 mCH367Data8Sta;					//F4H D7-D0端口状态寄存器
		u32 mCH367SData32Sta;               //F4H D31-D0端口状态寄存器
	};
	u8 mCH367Status;                        //F8H 杂项控制和状态寄存器
	u8 mCH367IO_RESV3;                      //F9H
	u8 mCH367Speed;                         //FAH 读写速度控制寄存器
	u8 mCH367PDataCtrl;                     //FBH 被动并行接口控制寄存器
	u8 mCH367IoTime;                        //FCH 硬件循环计数寄存器
	u8 mCH367SPICtrl;                       //FDH SPI控制寄存器
	u8 mCH367SPIData;                       //FEH SPI数据寄存器
	u8 mCH367IO_RESV4;                      //FFH
} mCH367_IO_REG, *mPCH367_IO_REG;

typedef	struct	_CH368_MEM_REG {			// CH367芯片的存储器空间
	u8			mCH368MemPort[0x8000];		// 0000H-7FFFH,共32768字节为标准的存储器单元
} mCH368_MEM_REG, *mPCH368_MEM_REG;

#define IOCTL_MAGIC 					'P'
#define CH36x_GET_IO_BASE_ADDR			_IOR(IOCTL_MAGIC, 0x80, u16)
#define CH36x_GET_MEM_BASE_ADDR			_IOR(IOCTL_MAGIC, 0x81, u16)

/* io/mem rw codes */
#define CH36x_READ_CONFIG_BYTE			_IOR(IOCTL_MAGIC, 0x82, u16)
#define CH36x_READ_CONFIG_WORD			_IOR(IOCTL_MAGIC, 0x83, u16)
#define CH36x_READ_CONFIG_DWORD			_IOR(IOCTL_MAGIC, 0x84, u16)
#define CH36x_WRITE_CONFIG_BYTE			_IOW(IOCTL_MAGIC, 0x85, u16)
#define CH36x_WRITE_CONFIG_WORD			_IOW(IOCTL_MAGIC, 0x86, u16)
#define CH36x_WRITE_CONFIG_DWORD		_IOW(IOCTL_MAGIC, 0x87, u16)
#define CH36x_READ_IO_BYTE				_IOR(IOCTL_MAGIC, 0x88, u16)
#define CH36x_READ_IO_WORD				_IOR(IOCTL_MAGIC, 0x89, u16)
#define CH36x_READ_IO_DWORD				_IOR(IOCTL_MAGIC, 0x8a, u16)
#define CH36x_WRITE_IO_BYTE				_IOW(IOCTL_MAGIC, 0x8b, u16)
#define CH36x_WRITE_IO_WORD				_IOW(IOCTL_MAGIC, 0x8c, u16)
#define CH36x_WRITE_IO_DWORD			_IOW(IOCTL_MAGIC, 0x8d, u16)
#define CH36x_READ_MEM_BYTE				_IOR(IOCTL_MAGIC, 0x8e, u16)
#define CH36x_READ_MEM_WORD				_IOR(IOCTL_MAGIC, 0x8f, u16)
#define CH36x_READ_MEM_DWORD			_IOR(IOCTL_MAGIC, 0x90, u16)
#define CH36x_WRITE_MEM_BYTE			_IOW(IOCTL_MAGIC, 0x91, u16)
#define CH36x_WRITE_MEM_WORD			_IOW(IOCTL_MAGIC, 0x92, u16)
#define CH36x_WRITE_MEM_DWORD			_IOW(IOCTL_MAGIC, 0x93, u16)
#define CH36x_READ_MEM_BLOCK			_IOR(IOCTL_MAGIC, 0x94, u16)
#define CH36x_WRITE_MEM_BLOCK			_IOW(IOCTL_MAGIC, 0x95, u16)

/* interrupt codes */
#define CH36x_ENABLE_INT				_IOW(IOCTL_MAGIC, 0x96, u16)
#define CH36x_DISABLE_INT				_IOW(IOCTL_MAGIC, 0x97, u16)

/* other codes */
#define CH36x_GET_CHIPTYPE				_IOR(IOCTL_MAGIC, 0x98, u16)
#define CH36x_GET_VERSION				_IOR(IOCTL_MAGIC, 0x99, u16)

static unsigned char g_dev_count = 0;
static struct class *ch36x_class = NULL;
static struct list_head g_private_head;
static int ch36x_major = 0x00;

struct ch36x_dev {
	struct list_head ch36x_dev_list;
	struct pci_dev *ch36x_pdev;
	struct cdev cdev;
	dev_t ch36x_dev;
	enum CHIP_TYPE chiptype;	
	unsigned long ioaddr;
	unsigned long iolen;
	void __iomem *memaddr;
	unsigned long memlen;
	int irq;
	char dev_file_name[20];
	struct mutex io_mutex;
	enum INTMODE intmode;
	struct fasync_struct *fasync;
};

static int ch36x_cfg_read(int type, unsigned char offset, unsigned long ch36x_arg,
	       struct pci_dev *pdev)
{
	int retval = 0;
	u8 read_byte;
	u16 read_word;
	u32 read_dword;

	switch (type) {
	case SIZE_BYTE:
		pci_read_config_byte(pdev, offset, &read_byte);
		retval = put_user(read_byte, (long __user *)ch36x_arg);
		break;
	case SIZE_WORD:
		pci_read_config_word(pdev, offset, &read_word);
		retval = put_user(read_word, (long __user *)ch36x_arg);
		break;
	case SIZE_DWORD:
		pci_read_config_dword(pdev, offset, &read_dword);
		retval = put_user(read_dword, (long __user *)ch36x_arg);
		break;
	default:
		return -EINVAL;
	}

	return retval;
}

static int ch36x_cfg_write(int type, unsigned char offset, unsigned long ch36x_arg,
		  struct pci_dev *pdev)
{
	int retval = 0;
	u8 write_byte;
	u16 write_word;
	u32 write_dword;

	switch (type) {
	case SIZE_BYTE:
		retval = get_user(write_byte, (long __user *)ch36x_arg);
		if (retval)
			goto out;
		pci_write_config_byte(pdev, offset, write_byte);
		break;
	case SIZE_WORD:
		retval = get_user(write_word, (long __user *)ch36x_arg);
		if (retval)
			goto out;
		pci_write_config_word(pdev, offset, write_word);
		break;
	case SIZE_DWORD:
		retval = get_user(write_dword, (long __user *)ch36x_arg);
		if (retval)
			goto out;
		pci_write_config_dword(pdev, offset, write_dword);
		break;
	default:
		return -EINVAL;
	}

out:
	return retval;
}

static int ch36x_io_read(int type, unsigned long addr, unsigned long ch36x_arg)
{
	int retval = 0;
	u32 ioval;

	switch (type) {
	case SIZE_BYTE:
		ioval = inb(addr);
		break;
	case SIZE_WORD:
		ioval = inw(addr);
		break;
	case SIZE_DWORD:
		ioval = inl(addr);
		break;
	default:
		return -EINVAL;
	}
	retval = put_user(ioval, (long __user *)ch36x_arg);

	return retval;
}

static int ch36x_io_write(int type, unsigned long addr, unsigned long ch36x_arg)
{
	int retval = 0;
	u8 write_byte;
	u16 write_word;
	u32 write_dword;

	switch (type) {
	case SIZE_BYTE:
		retval = get_user(write_byte, (long __user *)ch36x_arg);
		if (retval)
			goto out;
		outb(write_byte, addr);
		break;
	case SIZE_WORD:
		retval = get_user(write_word, (long __user *)ch36x_arg);
		if (retval)
			goto out;
		outw(write_word, addr);
		break;
	case SIZE_DWORD:
		retval = get_user(write_dword, (long __user *)ch36x_arg);
		if (retval)
			goto out;
		outl(write_dword, addr);
		break;
	default:
		return -EINVAL;
	}
	out:
		return retval;
}


static int ch36x_mmio_read(int type, void __iomem *addr, unsigned long ch36x_arg)
{
	int retval = 0;
	u32 ioval;

	switch (type) {
	case SIZE_BYTE:
		ioval = ioread8(addr);
		break;
	case SIZE_WORD:
		ioval = ioread16(addr);
		break;
	case SIZE_DWORD:
		ioval = ioread32(addr);
		break;
	default:
		return -EINVAL;
	}
	retval = put_user(ioval, (long __user *)ch36x_arg);

	return retval;
}

static int ch36x_mmio_write(int type, void __iomem *addr, unsigned long ch36x_arg)
{
	int retval = 0;
	u8 write_byte;
	u16 write_word;
	u32 write_dword;

	switch (type) {
	case SIZE_BYTE:
		retval = get_user(write_byte, (long __user *)ch36x_arg);
		if (retval)
			goto out;
		iowrite8(write_byte, addr);
		break;
	case SIZE_WORD:
		retval = get_user(write_word, (long __user *)ch36x_arg);
		if (retval)
			goto out;
		iowrite16(write_word, addr);
		break;
	case SIZE_DWORD:
		retval = get_user(write_dword, (long __user *)ch36x_arg);
		if (retval)
			goto out;
		iowrite32(write_dword, addr);
		break;
	default:
		return -EINVAL;
	}

out:
	return retval;
}

static int ch36x_mem_read_block(unsigned long addr, unsigned long buf,
		     unsigned long size)
{
	int retval = 0;
	
	retval = copy_to_user((char __user *)buf, (char *)addr, size);
	
	return retval;
}

static int ch36x_mem_write_block(unsigned long addr, unsigned long buf,
		      unsigned long size)
{
	int retval = 0;

	retval = copy_from_user((char *)addr, (char __user *)buf, size);

	return retval;
}

static irqreturn_t ch36x_isr(int irq, void *dev_id)
{
	unsigned char intval;
	struct ch36x_dev *ch36x_dev = (struct ch36x_dev *)dev_id;

	dev_vdbg(&ch36x_dev->ch36x_pdev->dev, "%s occurs\n", __func__);
	if (ch36x_dev->chiptype == CHIP_CH365) {
		intval = inb(ch36x_dev->ioaddr + offsetof(mCH365_IO_REG, mCh365IoCtrl));
		if (!(intval & CH365_IOCTRL_INTA_BIT))
			return IRQ_NONE;
	} else {
		intval = inb(ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367Status));
		switch (ch36x_dev->intmode) {
		case INT_RISING:
		case INT_FALLING:
			if (!(intval & CH367_MICSR_INTA_BIT))
				return IRQ_NONE;
			break;
		case INT_HIGH:
			if (!(intval & CH367_MICSR_INTS_BIT))
				return IRQ_NONE;
			break;
		case INT_LOW:
			if (intval & CH367_MICSR_INTS_BIT)
				return IRQ_NONE;
			break;
		default:
			return IRQ_NONE;
		}
	}
	kill_fasync(&ch36x_dev->fasync, SIGIO, POLL_IN);

	/* interrupt status clear */
	if (ch36x_dev->chiptype == CHIP_CH365) {
		outb(intval & ~CH365_IOCTRL_INTA_BIT, ch36x_dev->ioaddr + offsetof(mCH365_IO_REG, mCh365IoCtrl));
	} else {
		outb(intval & ~CH367_MICSR_INTA_BIT, ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367Status));
	}

	return IRQ_HANDLED;
}

static void ch36x_enable_interrupts(struct ch36x_dev *ch36x_dev, enum INTMODE mode)
{
	u8 ctrlval;
	u8 intval;
	
	if (ch36x_dev->chiptype == CHIP_CH365) {
		pci_read_config_byte(ch36x_dev->ch36x_pdev, CH365_STATUS_REG, &ctrlval);
		pci_write_config_byte(ch36x_dev->ch36x_pdev, CH365_STATUS_REG, ctrlval | CH365_STATUS_ENABLE_BIT);
	} else {
		ctrlval = inb(ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367IntCtr));
		switch (mode) {
			case INT_LOW:
				outb(ctrlval | TRIGGER_LOW, ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367IntCtr));
				break;
			case INT_HIGH:
				outb(ctrlval | TRIGGER_HIGH, ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367IntCtr));
				break;
			case INT_RISING:
				outb(ctrlval | TRIGGER_RISING, ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367IntCtr));
				break;
			case INT_FALLING:
				outb(ctrlval | TRIGGER_FALLING, ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367IntCtr));
				break;
			default:
				break;
		}
		ctrlval = inb(ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367IntCtr));
		outb(ctrlval | CH367_INTCR_INT_ENABLE_BIT, ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367IntCtr));
		intval = inb(ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367Status));
		outb(intval & ~CH367_MICSR_INTA_BIT, ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367Status));
	}
	ch36x_dev->intmode = mode;
}

static void ch36x_disable_interrupts(struct ch36x_dev *ch36x_dev)
{
	u8 ctrlval;
	u8 intval;
	
	if (ch36x_dev->chiptype == CHIP_CH365) {
		pci_read_config_byte(ch36x_dev->ch36x_pdev, CH365_STATUS_REG, &ctrlval);
		pci_write_config_byte(ch36x_dev->ch36x_pdev, CH365_STATUS_REG, ctrlval & ~CH365_STATUS_ENABLE_BIT);
	} else {
		ctrlval = inb(ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367IntCtr));
		outb(ctrlval & ~(CH367_INTCR_INT_POLAR_BIT | CH367_INTCR_INT_TYPE_BIT | CH367_INTCR_INT_ENABLE_BIT),
			ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367IntCtr));
		intval = inb(ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367Status));
		outb(intval & ~CH367_MICSR_INTA_BIT, ch36x_dev->ioaddr + offsetof(mCH367_IO_REG, mCH367Status));
	}
	ch36x_dev->intmode = INT_NONE;
}

static int ch36x_fops_open(struct inode *inode, struct file *fp)
{
	unsigned int minor = iminor(inode);
	struct list_head *pos;
	struct list_head *pos_tmp;
	struct ch36x_dev *ch36x_dev;

	list_for_each_safe(pos, pos_tmp, &g_private_head) {
		ch36x_dev = list_entry(pos, struct ch36x_dev, ch36x_dev_list);
		if (minor == MINOR(ch36x_dev->ch36x_dev)) {
			break;
		}
	}
	if (pos == &g_private_head) {
		pr_err("%s Can't find minor:%d line:%d", __func__, minor, __LINE__);
		return -ENODEV;
	}
	return 0;
}

static int ch36x_fops_release(struct inode *inode, struct file *fp)
{
	unsigned int minor = iminor(inode);
	struct list_head *pos;
	struct list_head *pos_tmp;
	struct ch36x_dev *ch36x_dev;

	list_for_each_safe(pos, pos_tmp, &g_private_head) {
		ch36x_dev = list_entry(pos, struct ch36x_dev, ch36x_dev_list);
		if (minor == MINOR(ch36x_dev->ch36x_dev)) {
			break;
		}
	}
	if (pos == &g_private_head) {
		pr_err("%s Can't find minor:%d line:%d", __func__, minor, __LINE__);
		return -ENODEV;
	}
	if (ch36x_dev->intmode)
		ch36x_disable_interrupts(ch36x_dev);
	
	return 0;
}

static ssize_t ch36x_fops_read(struct file *fp, char __user *buf,
				size_t len, loff_t *off)
{
	return 0;
}

static ssize_t ch36x_fops_write(struct file *fp, const char __user *buf,
		 size_t len, loff_t * off)
{
	return 0;
}

static int ch36x_fops_ioctl_do(struct ch36x_dev *ch36x_dev, unsigned int cmd,
		    unsigned long ch36x_arg)
{
	int retval = 0;
	unsigned long arg1;
	unsigned long arg2;
	unsigned long arg3;

	switch (cmd) {
	case CH36x_GET_CHIPTYPE:
		retval = put_user((unsigned long)ch36x_dev->chiptype, 
			(long __user *)ch36x_arg);
		break;
	case CH36x_GET_VERSION:
		retval = copy_to_user((char __user *)ch36x_arg, (char *)VERSION_DESC,
			strlen(VERSION_DESC));
		break;
	case CH36x_ENABLE_INT:
		get_user(arg1, (long __user *)ch36x_arg);
		ch36x_enable_interrupts(ch36x_dev, arg1);
		break;
    case CH36x_DISABLE_INT:
		ch36x_disable_interrupts(ch36x_dev);
        break;
	case CH36x_GET_IO_BASE_ADDR:
		retval = put_user(ch36x_dev->ioaddr,
				(long __user *)ch36x_arg);
		break;
	case CH36x_GET_MEM_BASE_ADDR:
		retval = put_user((unsigned long)ch36x_dev->memaddr,
				(long __user *)ch36x_arg);
		break;
	case CH36x_READ_CONFIG_BYTE:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		retval = ch36x_cfg_read(SIZE_BYTE, arg1, arg2,
					ch36x_dev->ch36x_pdev);
		break;
	case CH36x_READ_CONFIG_WORD:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		retval = ch36x_cfg_read(SIZE_WORD, arg1, arg2,
					ch36x_dev->ch36x_pdev);
		break;
	case CH36x_READ_CONFIG_DWORD:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		retval = ch36x_cfg_read(SIZE_DWORD, arg1, arg2,
					ch36x_dev->ch36x_pdev);
		break;
	case CH36x_WRITE_CONFIG_BYTE:
		get_user(arg1, (long __user *)ch36x_arg);
		arg2 = (unsigned long)((long __user *)ch36x_arg + 1);
		retval = ch36x_cfg_write(SIZE_BYTE, arg1, arg2,
				    ch36x_dev->ch36x_pdev);
		break;
	case CH36x_WRITE_CONFIG_WORD:
		get_user(arg1, (long __user *)ch36x_arg);
		arg2 = (unsigned long)((long __user *)ch36x_arg + 1);
		retval = ch36x_cfg_write(SIZE_WORD, arg1, arg2,
				    ch36x_dev->ch36x_pdev);
		break;
	case CH36x_WRITE_CONFIG_DWORD:
		get_user(arg1, (long __user *)ch36x_arg);
		arg2 = (unsigned long)((long __user *)ch36x_arg + 1);
		retval = ch36x_cfg_write(SIZE_DWORD, arg1, arg2,
				    ch36x_dev->ch36x_pdev);
		break;
	case CH36x_READ_IO_BYTE:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		retval = ch36x_io_read(SIZE_BYTE, arg1, arg2);
		break;
	case CH36x_READ_IO_WORD:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		retval = ch36x_io_read(SIZE_WORD, arg1, arg2);
		break;
	case CH36x_READ_IO_DWORD:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		retval = ch36x_io_read(SIZE_DWORD, arg1, arg2);
		break;
	case CH36x_WRITE_IO_BYTE:
		get_user(arg1, (long __user *)ch36x_arg);
		arg2 = (unsigned long)((long __user *)ch36x_arg + 1);
		retval = ch36x_io_write(SIZE_BYTE, arg1, arg2);
		break;
	case CH36x_WRITE_IO_WORD:
		get_user(arg1, (long __user *)ch36x_arg);
		arg2 = (unsigned long)((long __user *)ch36x_arg + 1);
		retval = ch36x_io_write(SIZE_WORD, arg1, arg2);
		break;
	case CH36x_WRITE_IO_DWORD:
		get_user(arg1, (long __user *)ch36x_arg);
		arg2 = (unsigned long)((long __user *)ch36x_arg + 1);
		retval = ch36x_io_write(SIZE_DWORD, arg1, arg2);
		break;
	case CH36x_READ_MEM_BYTE:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		retval = ch36x_mmio_read(SIZE_BYTE, (void __iomem *)arg1, arg2);
		break;
	case CH36x_READ_MEM_WORD:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		retval = ch36x_mmio_read(SIZE_WORD, (void __iomem *)arg1, arg2);
		break;
	case CH36x_READ_MEM_DWORD:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		retval = ch36x_mmio_read(SIZE_DWORD, (void __iomem *)arg1, arg2);
		break;
	case CH36x_WRITE_MEM_BYTE:
		get_user(arg1, (long __user *)ch36x_arg);
		arg2 = (unsigned long)((long __user *)ch36x_arg + 1);
		retval = ch36x_mmio_write(SIZE_BYTE, (void __iomem *)arg1, arg2);
		break;
	case CH36x_WRITE_MEM_WORD:
		get_user(arg1, (long __user *)ch36x_arg);
		arg2 = (unsigned long)((long __user *)ch36x_arg + 1);
		retval = ch36x_mmio_write(SIZE_WORD, (void __iomem *)arg1, arg2);
		break;
	case CH36x_WRITE_MEM_DWORD:
		get_user(arg1, (long __user *)ch36x_arg);
		arg2 = (unsigned long)((long __user *)ch36x_arg + 1);
		retval = ch36x_mmio_write(SIZE_DWORD, (void __iomem *)arg1, arg2);
		break;
	case CH36x_READ_MEM_BLOCK:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		get_user(arg3, ((long __user *)ch36x_arg + 2));
		retval = ch36x_mem_read_block(arg1, arg2, arg3);
		break;
	case CH36x_WRITE_MEM_BLOCK:
		get_user(arg1, (long __user *)ch36x_arg);
		get_user(arg2, ((long __user *)ch36x_arg + 1));
		get_user(arg3, ((long __user *)ch36x_arg + 2));
		retval = ch36x_mem_write_block(arg1, arg2, arg3);
		break;
	default:
		return -EINVAL;
	}

	return retval;
}

static long ch36x_fops_ioctl(struct file *fp, unsigned int ch36x_cmd,
		 unsigned long ch36x_arg)
{
	int retval = 0;
	struct list_head *pos;
	struct list_head *pos_tmp;
	struct ch36x_dev *ch36x_dev;
	unsigned int minor = iminor(fp->f_path.dentry->d_inode);
	
	list_for_each_safe(pos, pos_tmp, &g_private_head) {
		ch36x_dev = list_entry(pos, struct ch36x_dev, ch36x_dev_list);
		if (minor == MINOR(ch36x_dev->ch36x_dev)) {
			break;
		}
	}
	if (pos == &g_private_head) {
		pr_err("%s Can't find minor:%d line:%d", __func__, minor, __LINE__);
		return -ENODEV;
	}

	mutex_lock(&ch36x_dev->io_mutex);
	retval = ch36x_fops_ioctl_do(ch36x_dev, ch36x_cmd, ch36x_arg);
	mutex_unlock(&ch36x_dev->io_mutex);

	return retval;
}

static int ch36x_fops_fasync(int fd, struct file *fp, int on)
{
	struct list_head *pos;
	struct list_head *pos_tmp;
	struct ch36x_dev *ch36x_dev;
 	unsigned int minor = iminor(fp->f_path.dentry->d_inode);

	list_for_each_safe(pos, pos_tmp, &g_private_head) {
		ch36x_dev = 
			list_entry(pos, struct ch36x_dev, ch36x_dev_list);
		if( minor == MINOR(ch36x_dev->ch36x_dev)) {
			break;
		}
	}
	if(pos == &g_private_head) {
		pr_err("Fun:%s Can't find minor:0x%x line:%d", __FUNCTION__, minor,
				__LINE__);
		return -ENODEV;
	}

	return fasync_helper(fd, fp, on, &ch36x_dev->fasync);
}


static const struct file_operations ch36x_fops = {
	.owner		= THIS_MODULE,
	.open 		= ch36x_fops_open,
	.release 	= ch36x_fops_release,
	.read 		= ch36x_fops_read,
	.write 		= ch36x_fops_write,
	.unlocked_ioctl = ch36x_fops_ioctl,
	.fasync 	= ch36x_fops_fasync,
};

static void ch36x_dump_regs(struct ch36x_dev *ch36x_dev)
{
	u8 obyte;
	u8 offset;
	u8 configend;
	u8 iostart;
	u8 ioend;

	pr_debug("---------- g_dev_count: %d ----------\n", g_dev_count);
	
	pr_debug("\n---------- pci/pcie config space ----------\n");
	if (ch36x_dev->chiptype == CHIP_CH365) {
		configend = 0x42;
		iostart = 0xf0;
		ioend = 0xfc;
	} else {
		configend = 0x40;
		iostart = 0xe8;
		ioend = 0xfe;
	}
	for (offset = 0; offset <= configend; offset++) {
		pci_read_config_byte(ch36x_dev->ch36x_pdev, offset, &obyte);
		pr_debug("\toffset:0x%2x, val:0x%2x\n", offset, obyte);
	}

	pr_debug("\n---------- pci/pcie io space ----------\n");
	for (offset = iostart; offset <= ioend; offset++) {
		obyte = inb(ch36x_dev->ioaddr + offset);
		pr_debug("\toffset:0x%2x, val:0x%2x\n", offset, obyte);
	}

	return;
}

static void ch36x_unmap_device(struct pci_dev *pdev, struct ch36x_dev *ch36x_dev)
{
	pci_iounmap(pdev, ch36x_dev->memaddr);
}

static int ch36x_map_device(struct pci_dev *pdev, struct ch36x_dev *ch36x_dev)
{
	int ret = -ENOMEM;

	ch36x_dev->iolen = pci_resource_len(pdev, 0);
	ch36x_dev->memlen = pci_resource_len(pdev, 1);
	/* map the memory mapped i/o registers */
	ch36x_dev->ioaddr = pci_resource_start(pdev, 0);
	if (!ch36x_dev->ioaddr) {
		dev_err(&pdev->dev, "Error mapping io\n");
		goto out;
	}
	ch36x_dev->memaddr = pci_iomap(pdev, 1, ch36x_dev->memlen);
	if (ch36x_dev->memaddr == NULL) {
		dev_err(&pdev->dev, "Error mapping mem\n");
		goto out;
	}
	ch36x_dev->irq = pdev->irq;
	dev_info(&pdev->dev, "ch36x map succeed.\n");
	dev_vdbg(&pdev->dev, "***********I/O Port**********\n");
	dev_vdbg(&pdev->dev, "phy addr: 0x%lx\n", (unsigned long)pci_resource_start(pdev, 0));
	dev_vdbg(&pdev->dev, "io len: %ld\n", ch36x_dev->iolen);
	dev_vdbg(&pdev->dev, "***********I/O Memory**********\n");
	dev_vdbg(&pdev->dev, "phy addr: 0x%lx\n", (unsigned long)pci_resource_start(pdev, 1));
	dev_vdbg(&pdev->dev, "mapped addr: 0x%lx\n", (unsigned long)ch36x_dev->memaddr);
	dev_vdbg(&pdev->dev, "mem len: %ld\n", ch36x_dev->memlen);
	dev_info(&pdev->dev, "irq number is: %d", ch36x_dev->irq);

	return 0;

out:
	return ret;
}

static int ch36x_pci_probe(struct pci_dev *pdev,
						const struct pci_device_id *ent)
{
	int retval = -ENOMEM;
	struct ch36x_dev *ch36x_dev = NULL;
	struct device *dev;

	dev_info(&pdev->dev, "%s\n", __func__);
	ch36x_dev = kzalloc(sizeof(*ch36x_dev), GFP_KERNEL);
	if (!ch36x_dev)
		goto out;

	ch36x_dev->ch36x_pdev = pdev;
	if (pdev->device == CH365_DID)
		ch36x_dev->chiptype = CHIP_CH365;
	else if ((pdev->device == CH367_DID_SID_HIGH) || (pdev->device == CH367_DID_SID_LOW))
		ch36x_dev->chiptype = CHIP_CH367;
	else if (pdev->device == CH368_DID)
		ch36x_dev->chiptype = CHIP_CH368;

	retval = pci_enable_device(pdev);
	if (retval)
		goto free;
	
	pci_set_master(pdev);
	
	retval = pci_request_regions(pdev, CH36X_DRV_NAME);
	if (retval)
		goto disable;

	mutex_init(&ch36x_dev->io_mutex);

	retval = ch36x_map_device(pdev, ch36x_dev);
	if (retval)
		goto free_regions;

	pci_set_drvdata(pdev, ch36x_dev);
	sprintf(ch36x_dev->dev_file_name, "%s%c", CH36X_DRV_NAME, '0' + g_dev_count);
	retval = request_irq(ch36x_dev->irq, ch36x_isr, IRQF_SHARED,
				ch36x_dev->dev_file_name, (void *)ch36x_dev);
	if (retval)
		goto unmap;

	ch36x_dump_regs(ch36x_dev);

	cdev_init(&ch36x_dev->cdev, &ch36x_fops);
	ch36x_dev->cdev.owner = THIS_MODULE;
	ch36x_dev->ch36x_dev = MKDEV(ch36x_major, g_dev_count);
	retval = cdev_add(&ch36x_dev->cdev, ch36x_dev->ch36x_dev, 1);
	if (retval) {
		dev_err(&pdev->dev, "Could not add cdev\n");
		goto remove_isr;
	}
	
	dev = device_create(ch36x_class, &pdev->dev, ch36x_dev->ch36x_dev,
				NULL, "%s", ch36x_dev->dev_file_name);
	if (IS_ERR(dev))
		dev_err(&pdev->dev, "Could not create device node.\n");

	list_add_tail(&(ch36x_dev->ch36x_dev_list), &g_private_head);
	g_dev_count++;

	dev_info(&pdev->dev, "%s ch36x_pci_probe function finshed.", __func__);
	
	return 0;

remove_isr:
	free_irq(pdev->irq, ch36x_dev);
unmap:
	ch36x_unmap_device(pdev, ch36x_dev);
free_regions:
	pci_release_regions(pdev);
disable:
	pci_disable_device(pdev);
free:
	kfree(ch36x_dev);
out:
	return retval;
}

static void ch36x_pci_remove(struct pci_dev *pdev)
{
	struct ch36x_dev *ch36x_dev = pci_get_drvdata(pdev);

	if (!ch36x_dev)
		return;

	dev_info(&pdev->dev, "%s", __func__);
	device_destroy(ch36x_class, ch36x_dev->ch36x_dev);
	cdev_del(&ch36x_dev->cdev);
	free_irq(pdev->irq, ch36x_dev);
	ch36x_unmap_device(pdev, ch36x_dev);
	pci_release_regions(pdev);
	list_del(&(ch36x_dev->ch36x_dev_list));
	kfree(ch36x_dev);
}

static struct pci_device_id ch36x_id_table[] =	
{
	{ PCI_DEVICE(CH365_VID, CH365_DID) },
	{ PCI_DEVICE_SUB(CH367_VID, CH367_DID_SID_HIGH, CH367_SUB_VID, CH367_SUB_DID_SID_HIGH) },
	{ PCI_DEVICE_SUB(CH367_VID, CH367_DID_SID_LOW, CH367_SUB_VID, CH367_SUB_DID_SID_LOW) },
	{ PCI_DEVICE_SUB(CH368_VID, CH368_DID, CH368_SUB_VID, CH368_SUB_DID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, ch36x_id_table);	

static struct pci_driver ch36x_pci_driver = {
	.name 	  = CH36X_DRV_NAME,
	.id_table = ch36x_id_table,
	.probe    = ch36x_pci_probe,
	.remove   = ch36x_pci_remove,
};

static int __init ch36x_init(void)
{
	int error;
	dev_t dev;

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_DESC "\n");
	printk(KERN_INFO KBUILD_MODNAME ": " VERSION_DESC "\n");
	INIT_LIST_HEAD(&g_private_head);
	ch36x_class = class_create(THIS_MODULE, "ch36x_class");
	if (IS_ERR(ch36x_class)) {
		error = PTR_ERR(ch36x_class);
		goto out;
	}

	error = alloc_chrdev_region(&dev, 0, CH36X_MAX_NUM, CH36X_DRV_NAME);
	if (error)
		goto class_destroy;

	ch36x_major = MAJOR(dev);

	error =	pci_register_driver(&ch36x_pci_driver);
	if (error)
		goto chr_remove;

	return 0;
chr_remove:
	unregister_chrdev_region(dev, CH36X_MAX_NUM);
class_destroy:
	class_destroy(ch36x_class);
out:
	return error;
}

static void __exit ch36x_exit(void)
{
	printk(KERN_INFO KBUILD_MODNAME ": " "ch36x driver exit.\n");
	g_dev_count = 0;
	pci_unregister_driver(&ch36x_pci_driver);
	unregister_chrdev_region(MKDEV(ch36x_major, 0), CH36X_MAX_NUM);
	class_destroy(ch36x_class);
}

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");

module_init(ch36x_init);
module_exit(ch36x_exit);
