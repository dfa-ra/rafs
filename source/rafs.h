#ifndef RAFS_H
#define RAFS_H

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/dcache.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>

#define MODULE_NAME "rafs"

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)


// функции для работы с файлами в file.c
ssize_t rafs_read(struct file *file, char *buffer, size_t len, loff_t *offset);
ssize_t rafs_write(struct file *file, const char *buffer, size_t len, loff_t *offset);

// функции для работы с нодами из inode.c
struct inode* rafs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino, int ref_count);
struct dentry* rafs_lookup(struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag);
int rafs_create(struct user_namespace *mnt_userns, struct inode *parent_inode, struct dentry *child_dentry, umode_t mode, bool b);
int rafs_unlink(struct inode *parent_inode, struct dentry *child_dentry);
int rafs_mkdir(struct user_namespace *mnt_userns, struct inode *parent_inode, struct dentry *child_dentry, umode_t mode);
int rafs_rmdir(struct inode *parent_inode, struct dentry *child_dentry);
int rafs_link(struct dentry *old_dentry, struct inode *parent_dir, struct dentry *new_dentry);

// функции из super.c
int rafs_fill_super(struct super_block *sb, void *data, int silent);
struct dentry* rafs_mount(struct file_system_type* fs_type, int flags, const char* token, void* data);
void rafs_kill_sb(struct super_block* sb);

// функции из dir.c
int rafs_iterate(struct file* file, struct dir_context* ctx);


// екстерн структур
extern struct file_system_type rafs_fs_type;
extern struct inode_operations rafs_inode_ops;
extern struct file_operations rafs_dir_ops;
extern struct file_operations rafs_file_ops;

#endif
