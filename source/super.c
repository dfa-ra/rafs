#include "rafs.h"
#include "api/api.h"

extern struct inode* rafs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino);
extern struct inode_operations rafs_inode_ops;
extern struct file_operations rafs_dir_ops;


int rafs_fill_super(struct super_block *sb, void *data, int silent) {
    struct inode* inode;
    struct rafs_file_info *file_info;
    const char *token = (const char *)data;
    int ret;

    ret = rafs_backend_ops.init(sb, token);
    if (ret != 0) {
        return ret;
    }

    file_info = rafs_backend_ops.create(sb, 0, "", S_IFDIR | S_IRWXUGO);
    if (file_info == NULL) {
        rafs_backend_ops.destroy(sb);
        return -ENOMEM;
    }

    inode = rafs_get_inode(sb, NULL, file_info->mode, file_info->ino);
    if (inode == NULL) {
        rafs_backend_ops.free_file_info(file_info);
        rafs_backend_ops.destroy(sb);
        return -ENOMEM;
    }

    inode->i_op = &rafs_inode_ops;
    inode->i_fop = &rafs_dir_ops;
    inode->i_private = file_info;

    sb->s_root = d_make_root(inode);
    if (sb->s_root == NULL) {
        iput(inode);
        rafs_backend_ops.destroy(sb);
        return -ENOMEM;
    }

    LOG("return 0\n");
    return 0;
}


struct dentry* rafs_mount(
    struct file_system_type* fs_type,
    int flags,
    const char* token,
    void* data
) {
    struct dentry* ret = mount_nodev(fs_type, flags, (void*)token, rafs_fill_super);
    if (ret == NULL) {
        printk(KERN_ERR "[rafs] Can't mount file system\n");
    } else {
        printk(KERN_INFO "[rafs] Mounted successfully\n");
    }
    return ret;
}


void rafs_kill_sb(struct super_block* sb) {
    rafs_backend_ops.destroy(sb);
    printk(KERN_INFO "[rafs] rafs super block is destroyed. Unmount successfully.\n");
}


struct file_system_type rafs_fs_type = {
    .name = "rafs",
    .mount = rafs_mount,
    .kill_sb = rafs_kill_sb,
    .fs_flags = FS_USERNS_MOUNT,
};
