#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

static int __init rwProcMem_init(void)
{
	printk(KERN_EMERG "rwProcMem: HELLO\n");
	return 0;
}

static void __exit rwProcMem_exit(void)
{
	printk(KERN_EMERG "rwProcMem: GOODBYE\n");
}

module_init(rwProcMem_init);
module_exit(rwProcMem_exit);
MODULE_LICENSE("GPL");
