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

// структуры
struct rafs_file {
    struct list_head list;       // Связный список
    char name[256];              // Имя файла
    ino_t ino;                   // Номер inode
    ino_t parent_ino;            // Номер inode родительской директории
    umode_t mode;                // Тип и права доступа
    char *data;                  // Данные файла (содержимое)
    size_t size;                 // Размер данных файла
    size_t capacity;             // Емкость буфера данных
    unsigned int link_count;     // Количество жестких ссылок
};


struct rafs_sb_info {
    struct list_head file_list;  // Список файлов
    struct rw_semaphore rwsem;   // Семафор для синхронизации
    ino_t next_ino;              // Следующий свободный номер ноды
};


// функции для работы с файлами в file.c
struct rafs_file* rafs_file_find(struct super_block *sb, ino_t parent_ino, const char *name);
struct rafs_file* rafs_file_create(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode);
struct rafs_file* rafs_file_link(struct super_block *sb, ino_t parent_ino, const char *name, struct rafs_file *target_file);
void rafs_file_delete(struct super_block *sb, ino_t parent_ino, const char *name);
int rafs_file_is_empty_dir(struct super_block *sb, ino_t dir_ino);
void rafs_file_cleanup(struct super_block *sb);
size_t rafs_file_get_size(struct rafs_file *file);
ssize_t rafs_read(struct file *filp, char *buffer, size_t len, loff_t *offset);
ssize_t rafs_write(struct file *filp, const char *buffer, size_t len, loff_t *offset);

// функции для работы с нодами из inode.c
struct inode* rafs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino);
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
int rafs_iterate(struct file* filp, struct dir_context* ctx);


// екстерн структур
extern struct file_system_type rafs_fs_type;
extern struct inode_operations rafs_inode_ops;
extern struct file_operations rafs_dir_ops;
extern struct file_operations rafs_file_ops;

#endif
