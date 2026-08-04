#include "rwProcMem_module.h"
#include <linux/sched/signal.h>

#pragma pack(push,1)
struct ioctl_request {
    char     cmd;
    uint64_t param1;
    uint64_t param2;
    uint64_t param3;
    uint64_t buf_size;
};
#pragma pack(pop)

// ========== 初始化 ==========
static ssize_t OnCmdInitDeviceInfo(struct ioctl_request *hdr, char __user* buf) {
	long err = 0;
	do {
		err = init_mmap_lock_offset();
		if (err) break;
		err = init_map_count_offset();
	} while(0);
	return err;
}

// ========== 打开/关闭进程 ==========
static ssize_t OnCmdOpenProcess(struct ioctl_request *hdr, char __user* buf) {
	uint64_t pid = hdr->param1, handle = 0;
	struct pid * proc_pid_struct = get_proc_pid_struct(pid);
	if (!proc_pid_struct)
		return -EINVAL;

	handle = (uint64_t)proc_pid_struct;
	if (!!x_copy_to_user((void*)buf, (void*)&handle, sizeof(handle)))
		return -EINVAL;
	return 0;
}

static ssize_t OnCmdCloseProcess(struct ioctl_request *hdr, char __user* buf) {
	struct pid * proc_pid_struct = (struct pid *)hdr->param1;
	release_proc_pid_struct(proc_pid_struct);
	return 0;
}

// ========== 读内存 ==========
static ssize_t OnCmdReadProcessMemory(struct ioctl_request *hdr, char __user* buf) {
	struct pid * proc_pid_struct = (struct pid *)hdr->param1;
	size_t proc_virt_addr = (size_t)hdr->param2;
	bool is_force_read = hdr->param3 == 1;
	size_t size = (size_t)hdr->buf_size;
	size_t read_size = 0;

	if (!is_force_read && !check_proc_map_can_read(proc_pid_struct, proc_virt_addr, size))
		return -EFAULT;

	while (read_size < size) {
		size_t phy_addr, pfn_sz;
		pte_t *pte;
		bool old_pte_can_read;

		phy_addr = get_proc_phy_addr(proc_pid_struct, proc_virt_addr + read_size, &pte);
		if (phy_addr == 0) break;

		old_pte_can_read = is_pte_can_read(pte);
		if (is_force_read) {
			if (!old_pte_can_read && !change_pte_read_status(pte, true))
				break;
		} else if (!old_pte_can_read) {
			break;
		}

		pfn_sz = size_inside_page(phy_addr,
			((size - read_size) > PAGE_SIZE) ? PAGE_SIZE : (size - read_size));
		read_ram_physical_addr(false, phy_addr, (char*)(buf + read_size), pfn_sz);

		if (is_force_read && !old_pte_can_read)
			change_pte_read_status(pte, false);

		read_size += pfn_sz;
	}
	return read_size;
}

// ========== 写内存 ==========
static ssize_t OnCmdWriteProcessMemory(struct ioctl_request *hdr, char __user* buf) {
	struct pid * proc_pid_struct = (struct pid *)hdr->param1;
	size_t proc_virt_addr = (size_t)hdr->param2;
	bool is_force_write = hdr->param3 == 1;
	size_t size = (size_t)hdr->buf_size;
	size_t write_size = 0;

	if (!is_force_write && !check_proc_map_can_write(proc_pid_struct, proc_virt_addr, size))
		return -EFAULT;

	while (write_size < size) {
		size_t phy_addr, pfn_sz;
		pte_t *pte;
		bool old_pte_can_write;

		phy_addr = get_proc_phy_addr(proc_pid_struct, proc_virt_addr + write_size, &pte);
		if (phy_addr == 0) break;

		old_pte_can_write = is_pte_can_write(pte);
		if (is_force_write) {
			if (!old_pte_can_write && !change_pte_write_status(pte, true))
				break;
		} else if (!old_pte_can_write) {
			break;
		}

		pfn_sz = size_inside_page(phy_addr,
			((size - write_size) > PAGE_SIZE) ? PAGE_SIZE : (size - write_size));
		write_ram_physical_addr(phy_addr, (char*)((size_t)buf + write_size), false, pfn_sz);

		if (is_force_write && !old_pte_can_write)
			change_pte_write_status(pte, false);

		write_size += pfn_sz;
	}
	return write_size;
}

// ========== 获取模块列表(含基址) ==========
static ssize_t OnCmdGetProcessMapsCount(struct ioctl_request *hdr, char __user* buf) {
	struct pid * proc_pid_struct = (struct pid *)hdr->param1;
	return get_proc_map_count(proc_pid_struct);
}

static ssize_t OnCmdGetProcessMapsList(struct ioctl_request *hdr, char __user* buf) {
	struct pid * proc_pid_struct = (struct pid *)hdr->param1;
	return get_proc_maps_list(false, proc_pid_struct, (void*)(buf), hdr->buf_size - 1);
}

// ========== 根据进程名查PID ==========
static ssize_t OnCmdFindPid(struct ioctl_request *hdr, char __user* buf) {
	struct task_struct *task;
	char name[16] = {0};
	pid_t pid = 0;
	size_t name_len = hdr->buf_size;

	if (name_len > 15) name_len = 15;
	if (x_copy_from_user(name, buf, name_len))
		return -EFAULT;

	rcu_read_lock();
	for_each_process(task) {
		if (strncmp(task->comm, name, 15) == 0) {
			pid = task->pid;
			break;
		}
	}
	rcu_read_unlock();

	if (pid == 0)
		return -ESRCH;

	if (x_copy_to_user(buf, &pid, sizeof(pid)))
		return -EFAULT;
	return 0;
}

// ========== 命令分发 ==========
static inline ssize_t DispatchCommand(struct ioctl_request *hdr, char __user* buf) {
	switch (hdr->cmd) {
	case CMD_INIT_DEVICE_INFO:       return OnCmdInitDeviceInfo(hdr, buf);
	case CMD_OPEN_PROCESS:           return OnCmdOpenProcess(hdr, buf);
	case CMD_READ_PROCESS_MEMORY:    return OnCmdReadProcessMemory(hdr, buf);
	case CMD_WRITE_PROCESS_MEMORY:   return OnCmdWriteProcessMemory(hdr, buf);
	case CMD_CLOSE_PROCESS:          return OnCmdCloseProcess(hdr, buf);
	case CMD_GET_PROCESS_MAPS_COUNT: return OnCmdGetProcessMapsCount(hdr, buf);
	case CMD_GET_PROCESS_MAPS_LIST:  return OnCmdGetProcessMapsList(hdr, buf);
	case CMD_FIND_PID:               return OnCmdFindPid(hdr, buf);
	default:                         return -EINVAL;
	}
}

// ========== 驱动入口 ==========
static ssize_t rwProcMem_read(struct file* filp, char __user* buf, size_t size, loff_t* ppos) {
	struct ioctl_request hdr = {0};
	size_t header_size = sizeof(hdr);

	if (size < header_size) return -EINVAL;
	if (x_copy_from_user(&hdr, buf, header_size)) return -EFAULT;
	if (size < header_size + hdr.buf_size) return -EINVAL;

	return DispatchCommand(&hdr, buf + header_size);
}

static int rwProcMem_dev_init(void) {
	g_rwProcMem_devp = x_kmalloc(sizeof(struct rwProcMemDev), GFP_KERNEL);
	memset(g_rwProcMem_devp, 0, sizeof(struct rwProcMemDev));

	g_rwProcMem_devp->proc_parent = proc_mkdir(CONFIG_PROC_NODE_AUTH_KEY, NULL);
	if (g_rwProcMem_devp->proc_parent) {
		g_rwProcMem_devp->proc_entry = proc_create(CONFIG_PROC_NODE_AUTH_KEY,
			S_IRUGO | S_IWUGO, g_rwProcMem_devp->proc_parent, &rwProcMem_proc_ops);
	}

	printk(KERN_EMERG "Hello\n");
	return 0;
}

static void rwProcMem_dev_exit(void) {
	if (g_rwProcMem_devp->proc_entry) {
		proc_remove(g_rwProcMem_devp->proc_entry);
		g_rwProcMem_devp->proc_entry = NULL;
	}
	if (g_rwProcMem_devp->proc_parent) {
		proc_remove(g_rwProcMem_devp->proc_parent);
		g_rwProcMem_devp->proc_parent = NULL;
	}
	kfree(g_rwProcMem_devp);
	printk(KERN_EMERG "Goodbye\n");
}

int __init init_module(void) { return rwProcMem_dev_init(); }
void __exit cleanup_module(void) { rwProcMem_dev_exit(); }

#ifndef CONFIG_MODULE_GUIDE_ENTRY
unsigned char* __check_(unsigned char* result, void *ptr, void *diag) { return result; }
unsigned char* __check_fail_(unsigned char *result) { return result; }
#endif

unsigned long __stack_chk_guard;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux");
MODULE_DESCRIPTION("Linux default module");
