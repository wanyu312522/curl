#ifndef PROC_MAPS_H_
#define PROC_MAPS_H_

#include <linux/pid.h>
#include <linux/types.h>
#include <linux/mm_types.h>
#if MY_LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,83)
#include <linux/sched/task.h>
#include <linux/sched/mm.h>
#endif
#if MY_LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
#include <linux/vma_iterator.h>
#endif

static inline int down_read_mmap_lock(struct mm_struct *mm);
static inline int up_read_mmap_lock(struct mm_struct *mm);
static inline size_t get_proc_map_count(struct pid* proc_pid_struct);
static inline int check_proc_map_can_read(struct pid* proc_pid_struct, size_t proc_virt_addr, size_t size);
static inline int check_proc_map_can_write(struct pid* proc_pid_struct, size_t proc_virt_addr, size_t size);
static int get_proc_maps_list(bool is_kernel_buf, struct pid* proc_pid_struct, char* buf, size_t buf_size);

//////////////////////////////////////////////////////////////////////////
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/limits.h>
#include <linux/dcache.h>
#include <asm/uaccess.h>
#include <linux/path.h>
#include <asm-generic/mman-common.h>
#include "api_proxy.h"
#include "proc_maps_auto_offset.h"
#include "ver_control.h"

#define MY_PATH_MAX_LEN 1024
#pragma pack(push,1)
struct map_entry {
    unsigned long start;
    unsigned long end;
    unsigned char flags[4];
    char path[MY_PATH_MAX_LEN];
};
#pragma pack(pop)

static inline size_t get_proc_map_count(struct pid* proc_pid_struct) {
	ssize_t accurate_offset;
	struct task_struct *task = pid_task(proc_pid_struct, PIDTYPE_PID);
	struct mm_struct *mm = get_task_mm(task);
	size_t count = 0;
	if (g_init_map_count_offset_success == false) {
		mmput(mm);
		return 0;
	}

	if (down_read_mmap_lock(mm) != 0)
		goto _exit;

	accurate_offset = (ssize_t)((size_t)&mm->map_count - (size_t)mm + g_map_count_offset);
	if (accurate_offset >= sizeof(struct mm_struct) - sizeof(ssize_t)) {
		mmput(mm);
		return 0;
	}
	count = *(int *)((size_t)mm + (size_t)accurate_offset);

	up_read_mmap_lock(mm);
_exit:
	mmput(mm);
	return count;
}

static inline int check_proc_map_can_read(struct pid* proc_pid_struct, size_t proc_virt_addr, size_t size) {
	struct task_struct *task = pid_task(proc_pid_struct, PIDTYPE_PID);
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	int res = 0;
	if (!task) return res;

	mm = get_task_mm(task);
	if (!mm) return res;

	if (down_read_mmap_lock(mm) != 0)
		goto _exit;

	vma = find_vma(mm, proc_virt_addr);
	if (vma && (vma->vm_flags & VM_READ) && (proc_virt_addr + size <= vma->vm_end))
		res = 1;

	up_read_mmap_lock(mm);
_exit:
	mmput(mm);
	return res;
}

static inline int check_proc_map_can_write(struct pid* proc_pid_struct, size_t proc_virt_addr, size_t size) {
	struct task_struct *task = pid_task(proc_pid_struct, PIDTYPE_PID);
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	int res = 0;
	if (!task) return res;

	mm = get_task_mm(task);
	if (!mm) return res;

	if (down_read_mmap_lock(mm) != 0) {
		mmput(mm);
		return res;
	}

	vma = find_vma(mm, proc_virt_addr);
	if (vma && (vma->vm_flags & VM_WRITE) && (proc_virt_addr + size <= vma->vm_end))
		res = 1;

	up_read_mmap_lock(mm);
	mmput(mm);
	return res;
}


/* ---------- is_stack helper ---------- */
#if MY_LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
#include <linux/mm_inline.h>
#endif

static int is_stack(struct vm_area_struct *vma) {
	return vma->vm_start <= vma->vm_mm->start_stack &&
	       vma->vm_end >= vma->vm_mm->start_stack;
}


/* ---------- get_proc_maps_list (per kernel version) ---------- */

#if MY_LINUX_VERSION_CODE < KERNEL_VERSION(6,1,0)
/* pre-6.1: iterate via mm->mmap linked list */

static int get_proc_maps_list(bool is_kernel_buf, struct pid* proc_pid_struct, char* buf, size_t buf_size) {
	struct task_struct *task;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	char *path_buf = NULL;
	struct map_entry *entry = NULL;
	int success_cnt = 0, ret = 0;
	size_t copy_pos, end_pos;

	task = pid_task(proc_pid_struct, PIDTYPE_PID);
	if (!task) { ret = -ESRCH; goto out; }

	mm = get_task_mm(task);
	if (!mm) { ret = -EINVAL; goto out; }

	if (is_kernel_buf) memset(buf, 0, buf_size);

	path_buf = x_kmalloc(MY_PATH_MAX_LEN, GFP_KERNEL);
	if (!path_buf) { ret = -ENOMEM; goto out_mm; }
	entry = x_kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) { ret = -ENOMEM; goto out_kpath; }

	copy_pos = (size_t)buf;
	end_pos  = (size_t)((size_t)buf + buf_size);

	if (down_read_mmap_lock(mm) != 0) { ret = -EBUSY; goto out_kentry; }

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		struct file *vm_file;
		if (copy_pos + sizeof(*entry) >= end_pos) break;

		memset(entry, 0, sizeof(*entry));
		entry->start = vma->vm_start;
		entry->end   = vma->vm_end;
		entry->flags[0] = (vma->vm_flags & VM_READ)     ? 1 : 0;
		entry->flags[1] = (vma->vm_flags & VM_WRITE)    ? 1 : 0;
		entry->flags[2] = (vma->vm_flags & VM_EXEC)     ? 1 : 0;
		entry->flags[3] = (vma->vm_flags & VM_MAYSHARE) ? 1 : 0;

		memset(entry->path, 0, sizeof(entry->path));
		vm_file = get_vm_file(vma);
		if (vm_file) {
			char *path;
			memset(path_buf, 0, MY_PATH_MAX_LEN);
			path = d_path(&vm_file->f_path, path_buf, MY_PATH_MAX_LEN);
			if (path > 0)
				strncat(entry->path, path, sizeof(entry->path) - 1);
		} else if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso) {
			snprintf(entry->path, sizeof(entry->path), "%s[vdso]", entry->path);
		} else {
			if (vma->vm_start <= mm->brk && vma->vm_end >= mm->start_brk)
				snprintf(entry->path, sizeof(entry->path), "%s[heap]", entry->path);
			else if (is_stack(vma))
				snprintf(entry->path, sizeof(entry->path), "%s[stack]", entry->path);
		}

		if (is_kernel_buf)
			memcpy((void *)copy_pos, entry, sizeof(*entry));
		else if (x_copy_to_user((void *)copy_pos, entry, sizeof(*entry)))
			break;
		copy_pos += sizeof(*entry);
		success_cnt++;
	}
	up_read_mmap_lock(mm);
	ret = success_cnt;

out_kentry: kfree(entry);
out_kpath:  kfree(path_buf);
out_mm:     mmput(mm);
out:        return ret;
}

#elif MY_LINUX_VERSION_CODE < KERNEL_VERSION(6,6,0)
/* 6.1.x: use VMA_ITERATOR / for_each_vma */

static int get_proc_maps_list(bool is_kernel_buf, struct pid* proc_pid_struct, char* buf, size_t buf_size) {
	struct task_struct *task;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	char *path_buf = NULL;
	struct map_entry *entry = NULL;
	int success_cnt = 0, ret = 0;
	size_t copy_pos, end_pos;

	task = pid_task(proc_pid_struct, PIDTYPE_PID);
	if (!task) { ret = -ESRCH; goto out; }

	mm = get_task_mm(task);
	if (!mm) { ret = -EINVAL; goto out; }

	if (is_kernel_buf) memset(buf, 0, buf_size);

	path_buf = x_kmalloc(MY_PATH_MAX_LEN, GFP_KERNEL);
	if (!path_buf) { ret = -ENOMEM; goto out_mm; }
	entry = x_kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) { ret = -ENOMEM; goto out_kpath; }

	copy_pos = (size_t)buf;
	end_pos  = (size_t)((size_t)buf + buf_size);

	if (down_read_mmap_lock(mm) != 0) { ret = -EBUSY; goto out_kentry; }

	{
		VMA_ITERATOR(iter, mm, 0);
		for_each_vma(iter, vma) {
			struct file *vm_file;
			if (copy_pos + sizeof(*entry) >= end_pos) break;

			memset(entry, 0, sizeof(*entry));
			entry->start = vma->vm_start;
			entry->end   = vma->vm_end;
			entry->flags[0] = (vma->vm_flags & VM_READ)     ? 1 : 0;
			entry->flags[1] = (vma->vm_flags & VM_WRITE)    ? 1 : 0;
			entry->flags[2] = (vma->vm_flags & VM_EXEC)     ? 1 : 0;
			entry->flags[3] = (vma->vm_flags & VM_MAYSHARE) ? 1 : 0;

			memset(entry->path, 0, sizeof(entry->path));
			vm_file = get_vm_file(vma);
			if (vm_file) {
				char *path;
				memset(path_buf, 0, MY_PATH_MAX_LEN);
				path = d_path(&vm_file->f_path, path_buf, MY_PATH_MAX_LEN);
				if (path > 0)
					strncat(entry->path, path, sizeof(entry->path) - 1);
			} else if (!vma->vm_mm) {
				snprintf(entry->path, sizeof(entry->path), "%s[vdso]", entry->path);
			} else if (vma->vm_start <= mm->brk && vma->vm_end >= mm->start_brk) {
				snprintf(entry->path, sizeof(entry->path), "%s[heap]", entry->path);
			} else if (is_stack(vma)) {
				snprintf(entry->path, sizeof(entry->path), "%s[stack]", entry->path);
			}

			if (is_kernel_buf)
				memcpy((void *)copy_pos, entry, sizeof(*entry));
			else if (x_copy_to_user((void *)copy_pos, entry, sizeof(*entry)))
				break;
			copy_pos += sizeof(*entry);
			success_cnt++;
		}
	}
	up_read_mmap_lock(mm);
	ret = success_cnt;

out_kentry: kfree(entry);
out_kpath:  kfree(path_buf);
out_mm:     mmput(mm);
out:        return ret;
}

#else
/* 6.6+: vma_is_initial_heap / vma_is_initial_stack helpers */

struct anon_vma_name * __weak anon_vma_name(struct vm_area_struct* vma) { return NULL; }

static int get_proc_maps_list(bool is_kernel_buf, struct pid* proc_pid_struct, char* buf, size_t buf_size) {
	struct task_struct *task;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	char *path_buf = NULL;
	struct map_entry *entry = NULL;
	int success_cnt = 0, ret = 0;
	size_t copy_pos, end_pos;

	task = pid_task(proc_pid_struct, PIDTYPE_PID);
	if (!task) { ret = -ESRCH; goto out; }

	mm = get_task_mm(task);
	if (!mm) { ret = -EINVAL; goto out; }

	if (is_kernel_buf) memset(buf, 0, buf_size);

	path_buf = x_kmalloc(MY_PATH_MAX_LEN, GFP_KERNEL);
	if (!path_buf) { ret = -ENOMEM; goto out_mm; }
	entry = x_kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) { ret = -ENOMEM; goto out_kpath; }

	copy_pos = (size_t)buf;
	end_pos  = (size_t)((size_t)buf + buf_size);

	if (down_read_mmap_lock(mm) != 0) { ret = -EBUSY; goto out_kentry; }

	{
		VMA_ITERATOR(iter, mm, 0);
		for_each_vma(iter, vma) {
			struct file *vm_file;
			struct anon_vma_name *anon_name = NULL;
			if (copy_pos + sizeof(*entry) >= end_pos) break;

			memset(entry, 0, sizeof(*entry));
			entry->start = vma->vm_start;
			entry->end   = vma->vm_end;
			entry->flags[0] = (vma->vm_flags & VM_READ)     ? 1 : 0;
			entry->flags[1] = (vma->vm_flags & VM_WRITE)    ? 1 : 0;
			entry->flags[2] = (vma->vm_flags & VM_EXEC)     ? 1 : 0;
			entry->flags[3] = (vma->vm_flags & VM_MAYSHARE) ? 1 : 0;

			memset(entry->path, 0, sizeof(entry->path));
			vm_file = get_vm_file(vma);
			if (vm_file) {
				char *path;
				memset(path_buf, 0, MY_PATH_MAX_LEN);
				path = d_path(&vm_file->f_path, path_buf, MY_PATH_MAX_LEN);
				if (path > 0)
					strncat(entry->path, path, sizeof(entry->path) - 1);
			} else if (!vma->vm_mm) {
				snprintf(entry->path, sizeof(entry->path), "%s[vdso]", entry->path);
			} else if (vma_is_initial_heap(vma)) {
				snprintf(entry->path, sizeof(entry->path), "%s[heap]", entry->path);
			} else if (vma_is_initial_stack(vma)) {
				snprintf(entry->path, sizeof(entry->path), "%s[stack]", entry->path);
			} else {
				anon_name = anon_vma_name(vma);
				if (anon_name)
					snprintf(entry->path, sizeof(entry->path), "[anon:%s]", anon_name->name);
			}

			if (is_kernel_buf)
				memcpy((void *)copy_pos, entry, sizeof(*entry));
			else if (x_copy_to_user((void *)copy_pos, entry, sizeof(*entry)))
				break;
			copy_pos += sizeof(*entry);
			success_cnt++;
		}
	}
	up_read_mmap_lock(mm);
	ret = success_cnt;

out_kentry: kfree(entry);
out_kpath:  kfree(path_buf);
out_mm:     mmput(mm);
out:        return ret;
}
#endif

#endif /* PROC_MAPS_H_ */
