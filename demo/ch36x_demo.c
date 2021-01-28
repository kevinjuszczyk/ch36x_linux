/* 
 * ch365/ch367/ch368 PCI/PCIE demo - Copyright (C) 2021 WCH Corporation.
 * Author: TECH39 <zhangj@wch.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Cross-compile with cross-gcc -I /path/to/cross-kernel/include
 *
 * Version: V1.1
 * 
 * Update Log:
 * V1.0 - initial version
 * V1.1 - called new APIs within ch36x_lib
 *
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>   
#include <string.h>
#include <sys/types.h> 
#include "ch36x_lib.h"

static const char *device = "/dev/ch36xpci0";

static void ch36x_demo_io_operate(int fd, unsigned long ioaddr)
{
	int ret;
	char c;
	int ibyte;
	int offset;
	uint8_t obyte;

	printf("\n---------- IO read write test ----------\n");	
	while (1) {
		printf("press w to write one byte, r to read one byte, q for quit.\n");
		scanf("%c", &c);
		getchar();
		if (c == 'q')
			break;
		switch (c) {
		case 'w':
			printf("input offset of io:\n");
			scanf("%x", &offset);
			getchar();
			printf("input write value:\n");
			scanf("%x", &ibyte);
			getchar();
			ret = ch36x_write_io_byte(fd, ioaddr + offset, (uint8_t)ibyte);
			if (ret != 0)
				printf("io write fail.\n");
			break;
		case 'r':
			printf("input offset of io:\n");
			scanf("%x", &offset);
			getchar();
			ret = ch36x_read_io_byte(fd, ioaddr + offset, &obyte);
			if (ret != 0)
				printf("io read fail.\n");
			printf("read byte: 0x%2x\n", obyte);
			break;
		default:
			break;
		}
	}
}

static void ch36x_demo_mem_operate(int fd, unsigned long memaddr)
{
	int ret;
	char c;
	int ibyte;
	int offset;
	uint8_t obyte;

	printf("\n---------- Memory read write test ----------\n");	
	while (1) {
		printf("press w to write one byte, r to read one byte, q for quit.\n");
		scanf("%c", &c);
		getchar();
		if (c == 'q')
			break;
		switch (c) {
		case 'w':
			printf("input offset of mem:\n");
			scanf("%x", &offset);
			getchar();
			printf("input write value:\n");
			scanf("%x", &ibyte);
			getchar();
			ret = ch36x_write_mem_byte(fd, memaddr + offset, (uint8_t)ibyte);
			if (ret != 0)
				printf("memory write fail.\n");
			break;
		case 'r':
			printf("input offset of mem:\n");
			scanf("%x", &offset);
			getchar();
			ret = ch36x_read_mem_byte(fd, memaddr + offset, &obyte);
			if (ret != 0)
				printf("memory read fail.\n");
			printf("read byte: 0x%2x\n", obyte);
			break;
		default:
			break;
		}
	}
}

static void ch36x_demo_config_operate(int fd)
{
	int ret;
	char c;
	int ibyte;
	int offset;
	uint8_t obyte;

	printf("\n---------- Config space read write test ----------\n");	
	while (1) {
		printf("press w to write one byte, r to read one byte, q for quit.\n");
		scanf("%c", &c);
		getchar();
		if (c == 'q')
			break;
		switch (c) {
		case 'w':
			printf("input offset of config space:\n");
			scanf("%x", &offset);
			getchar();
			printf("input write value:\n");
			scanf("%x", &ibyte);
			getchar();
			ret = ch36x_write_config_byte(fd, offset, ibyte);
			if (ret != 0)
				printf("config space write fail.\n");
			break;
		case 'r':
			printf("input offset of config space:\n");
			scanf("%x", &offset);
			getchar();
			ret = ch36x_read_config_byte(fd, offset, &obyte);
			if (ret != 0)
				printf("config space read fail.\n");
			printf("read byte: 0x%2x\n", obyte);
			break;
		default:
			break;
		}
	}
}

static void ch36x_dmeo_isr_handler(int signo)
{
	static int int_times = 0;
	
	printf("ch36x interrupt times: %d\n", int_times++);
}

static void ch36x_demo_isr_enable(int fd)
{
	int ret;
	enum INTMODE mode = INT_FALLING;

	ret = ch36x_enable_isr(fd, mode);
	if (ret != 0) {
		printf("ch36x_enable_isr failed.\n");
		return;
	}
	ch36x_set_int_routine(fd, ch36x_dmeo_isr_handler);
}

static void ch36x_demo_isr_disable(int fd)
{
	int ret;

	ret = ch36x_disable_isr(fd);
	if (ret != 0) {
		printf("ch36x_disable_isr failed.\n");
		return;
	}
	ch36x_set_int_routine(fd, NULL);
}

int main(int argc, char *argv[])
{
	int fd;
	int ret;
	char c;
	enum CHIP_TYPE chiptype;
	unsigned long iobase;
	unsigned long membase;

	fd = ch36x_open(device);
	if (fd < 0) {
		printf("ch36x_open error.\n");
		goto exit;
	}

	ret = ch36x_get_chiptype(fd, &chiptype);
	if (ret != 0) {
		printf("ch36x_get_chiptype error.\n");
		goto exit;
	}
	switch (chiptype) {
	case CHIP_CH365:
		printf("current chip model: CH365.\n");
		break;
	case CHIP_CH367:
		printf("current chip model: CH367.\n");
		break;
	case CHIP_CH368:
		printf("current chip model: CH368.\n");
		break;
	}

	ret = ch36x_get_ioaddr(fd, &iobase);
	if (ret != 0) {
		printf("ch36x_get_ioaddr error.\n");
		goto exit;
	}
	printf("iobase:%lx\n", iobase);

	if (chiptype == CHIP_CH368) {
		ret = ch36x_get_memaddr(fd, &membase);
		if (ret != 0) {
			printf("ch36x_get_memaddr error.\n");
			goto exit;
		}
		printf("membase:%lx\n", membase);
	}
	
	while (1) {
		printf("press c to operate config space, m to operate memory space, "
				"i to operate io space, e to enable interrupt, "
				"d to disable interrpt, q for quit.\n");
		scanf("%c", &c);
		getchar();
		if (c == 'q')
			break;
		switch (c) {
		case 'i':
			ch36x_demo_io_operate(fd, iobase);
			break;
		case 'm':
			if (chiptype == CHIP_CH368)
				ch36x_demo_mem_operate(fd, membase);
			else
				printf("chip not support.\n");
			break;
		case 'c':
			ch36x_demo_config_operate(fd);
			break;
		case 'e':
			ch36x_demo_isr_enable(fd);
			break;
		case 'd':
			ch36x_demo_isr_disable(fd);
			break;
		default:
			break;
		}
	}
 	
	ret = ch36x_close(fd);
	if (ret != 0) {
		printf("ch36x_close error.\n");
		goto exit;
	}

exit:
	return ret;
}


