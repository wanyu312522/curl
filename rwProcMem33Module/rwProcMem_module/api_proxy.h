#ifndef API_PROXY_H_
#define API_PROXY_H_
#include "ver_control.h"
#include "linux_kernel_api.h"
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

static inline struct task_struct* x_get_current(void) {
	unsigned long sp_el0;
	asm ("mrs %0, sp_el0" : "=r" (sp_el0));
	return (struct task_struct *)sp_el0;
}

static inline void * x_kmalloc(size_t size, gfp_t flags) {
	return kmalloc(size, flags);
}

static inline unsigned long x_copy_from_user(void *to, const void __user *from, unsigned long n) {
	return copy_from_user(to, from, n);
}

static inline unsigned long x_copy_to_user(void __user *to, const void *from, unsigned long n) {
	return copy_to_user(to, from, n);
}

#endif /* API_PROXY_H_ */
