#include "rafs.h"
#include "api/api.h"
#include <linux/string.h>

int rafs_iterate(struct file* filp, struct dir_context* ctx) {
    struct dentry* dentry;
    struct inode* inode;
    ino_t ino_val;

    if (filp == NULL || ctx == NULL) {
        return 0;
    }

    dentry = filp->f_path.dentry;
    if (dentry == NULL) {
        return 0;
    }

    inode = dentry->d_inode;
    if (inode == NULL) {
        return 0;
    }

    ino_val = inode->i_ino;

    return rafs_backend_ops.readdir(inode->i_sb, ino_val, ctx);
}

struct file_operations rafs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = rafs_iterate,
};
