#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/statfs.h>

#define MODULE_NAME "rafs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("heathens");
MODULE_DESCRIPTION("A simple ra FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

void rafs_kill_sb(struct super_block* sb);
struct dentry* rafs_mount(
  struct file_system_type* fs_type,
  int flags,
  const char* token,
  void* data
);

struct file_system_type rafs_fs_type = {
  .name = "rafs",
  .mount = rafs_mount,
  .kill_sb = rafs_kill_sb,
};

static int __init rafs_init(void) {
  int ret;

  LOG("RAFS joined the kernel\n");

  ret = register_filesystem(&rafs_fs_type);
  if (ret) {
    pr_err("[" MODULE_NAME "]: Failed to register filesystem: %d\n", ret);
    return ret;
  }

  LOG("Filesystem registered successfully\n");
  return 0;
}

static void __exit rafs_exit(void) {
  unregister_filesystem(&rafs_fs_type);
    LOG("RAFS left the kernel\n");
}

struct inode* rafs_get_inode(
  struct super_block* sb,
  const struct inode* dir,
  umode_t mode,
  int i_ino
) {
  struct inode *inode = new_inode(sb);
  if (inode != NULL) {
    inode_init_owner(&init_user_ns, inode, dir, mode);
  }

  inode->i_ino = i_ino;
  return inode;
}

int rafs_fill_super(struct super_block *sb, void *data, int silent) {
  struct inode* inode = rafs_get_inode(sb, NULL, S_IFDIR, 1000);

  sb->s_root = d_make_root(inode);
  if (sb->s_root == NULL) {
    return -ENOMEM;
  }

  printk(KERN_INFO "return 0\n");
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
    printk(KERN_ERR "Can't mount file system");
  } else {
    printk(KERN_INFO "Mounted successfuly");
  }
  return ret;
}

void rafs_kill_sb(struct super_block* sb) {
  printk(KERN_INFO "rafs super block is destroyed. Unmount successfully.\n");
}

module_init(rafs_init);
module_exit(rafs_exit);