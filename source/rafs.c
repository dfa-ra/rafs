#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include "rafs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

extern struct file_system_type rafs_fs_type;

static int __init rafs_init(void) {
    int ret;
    LOG("rafs joined the kernel\n");
    
    ret = register_filesystem(&rafs_fs_type);
    if (ret != 0) { LOG("Failed to register filesystem\n"); }

    return ret;
}


static void __exit rafs_exit(void) {
    unregister_filesystem(&rafs_fs_type);
    LOG("rafs left the kernel\n");
}

module_init(rafs_init);
module_exit(rafs_exit);
