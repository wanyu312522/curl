#ifndef RWPROCMEM_H_
#define RWPROCMEM_H_
#include <linux/module.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/proc_fs.h>

#include "api_proxy.h"
#include "phy_mem.h"
#include "proc_maps.h"
#include "ver_control.h"

enum {
	CMD_INIT_DEVICE_INFO = 1, 	// 初始化设备信息
	CMD_OPEN_PROCESS, 			// 打开进程
	CMD_READ_PROCESS_MEMORY,    // 读取进程内存
	CMD_WRITE_PROCESS_MEMORY,   // 写入进程内存
	CMD_CLOSE_PROCESS, 			// 关闭进程
	CMD_GET_PROCESS_MAPS_COUNT, // 获取进程模块数量
	CMD_GET_PROCESS_MAPS_LIST, 	// 获取进程模块列表(含基址)
	CMD_FIND_PID,				// 根据进程名查找PID
};

struct rwProcMemDev {
	struct proc_dir_entry *proc_parent;
	struct proc_dir_entry *proc_entry;
};
static struct rwProcMemDev *g_rwProcMem_devp;

static ssize_t rwProcMem_read(struct file* filp, char __user* buf, size_t size, loff_t* ppos);
static const struct proc_ops rwProcMem_proc_ops = {
    .proc_read    = rwProcMem_read,
};

#endif /* RWPROCMEM_H_ */
