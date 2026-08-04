#ifndef LINUX_KERNEL_API_H_
#define LINUX_KERNEL_API_H_
#include "ver_control.h"
#include <linux/module.h>

#if MY_LINUX_VERSION_CODE < KERNEL_VERSION(5,8,0)
long probe_kernel_read(void* dst, const void* src, size_t size);
static long x_probe_kernel_read(void* bounce, const char* ptr, size_t sz) {
    return probe_kernel_read(bounce, ptr, sz);
}
#else
long copy_from_kernel_nofault(void* dst, const void* src, size_t size);
static long x_probe_kernel_read(void* bounce, const char* ptr, size_t sz) {
    return copy_from_kernel_nofault(bounce, ptr, sz);
}
#endif

#if MY_LINUX_VERSION_CODE < KERNEL_VERSION(6,6,0)
static inline pte_t x_pte_mkwrite(pte_t pte) {
    return pte_mkwrite(pte);
}
#else
static inline pte_t x_pte_mkwrite(pte_t pte) {
    struct vm_area_struct vma = {.vm_flags = VM_READ};
    return pte_mkwrite(pte, &vma);
}
#endif

#endif /* LINUX_KERNEL_API_H_ */
