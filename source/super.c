#include "rafs.h"

extern struct inode* rafs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino);
extern struct inode_operations rafs_inode_ops;
extern struct file_operations rafs_dir_ops;
extern void rafs_file_cleanup(struct super_block *sb);


int rafs_fill_super(struct super_block *sb, void *data, int silent) {
    struct rafs_sb_info *sbi;
    struct inode* inode;

    sbi = kmalloc(sizeof(struct rafs_sb_info), GFP_KERNEL);
    if (sbi == NULL) {
        return -ENOMEM;
    }

    INIT_LIST_HEAD(&sbi->file_list);
    init_rwsem(&sbi->rwsem);
    sbi->next_ino = 1001;
    
    sb->s_fs_info = sbi;

    inode = rafs_get_inode(sb, NULL, S_IFDIR | S_IRWXUGO, 1000);
    if (inode == NULL) {
        kfree(sbi);
        return -ENOMEM;
    }

    inode->i_op = &rafs_inode_ops;
    inode->i_fop = &rafs_dir_ops;

    sb->s_root = d_make_root(inode);
    if (sb->s_root == NULL) {
        iput(inode);
        kfree(sbi);
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
    struct dentry* ret = mount_nodev(fs_type, flags, data, rafs_fill_super);
    if (ret == NULL) {
        printk(KERN_ERR "[rafs] Can't mount file system\n");
    } else {
        printk(KERN_INFO "[rafs] Mounted successfully\n");
    }
    return ret;
}


void rafs_kill_sb(struct super_block* sb) {
    rafs_file_cleanup(sb);
    printk(KERN_INFO "[rafs] rafs super block is destroyed. Unmount successfully.\n");
}


struct file_system_type rafs_fs_type = {
    .name = "rafs",
    .mount = rafs_mount,
    .kill_sb = rafs_kill_sb,
    .fs_flags = FS_USERNS_MOUNT,
};
