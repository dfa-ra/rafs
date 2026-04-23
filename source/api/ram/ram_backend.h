#ifndef RAFS_RAM_BACKEND_H
#define RAFS_RAM_BACKEND_H

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include "../../rafs.h"

struct ram_inode {
    struct list_head list;        // связный список всех inode
    ino_t ino;                    // номер inode
    umode_t mode;                 // тип и права доступа
    char *data;                   // данные файла (содержимое)
    size_t size;                  // размер данных файла
    size_t capacity;              // емкость буфера данных
    int nlink;           // количество hardlink на этот inode
};

struct ram_dentry {
    struct list_head list;        // связный список всех dentries
    ino_t parent_ino;             // номер inode родительской директории
    char name[256];               // имя файла
    struct ram_inode *inode;      // указатель на inode
};

struct ram_sb_info {
    struct list_head inode_list;  // список inode
    struct list_head dentry_list; // список dentries
    struct rw_semaphore rwsem;    // семафор для синхронизации
    ino_t next_ino;               // следующий свободный номер inode
    char *token;                  // токен для аутентификации (для совместимости, RAM backend игнорирует)
};


int ram_backend_init(struct super_block *sb, const char *token);
void ram_backend_destroy(struct super_block *sb);
struct rafs_file_info* ram_backend_lookup(struct super_block *sb, ino_t parent_ino, const char *name);
struct rafs_file_info* ram_backend_create(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode);
int ram_backend_unlink(struct super_block *sb, ino_t parent_ino, const char *name);
int ram_backend_rmdir(struct super_block *sb, ino_t parent_ino, const char *name);
struct rafs_file_info* ram_backend_link(struct super_block *sb, ino_t parent_ino, const char *name, struct rafs_file_info *target);
int ram_backend_is_empty_dir(struct super_block *sb, ino_t dir_ino);
ssize_t ram_backend_read(struct rafs_file_info *file, char *buffer, size_t len, loff_t offset);
ssize_t ram_backend_write(struct rafs_file_info *file, const char *buffer, size_t len, loff_t offset);
size_t ram_backend_get_size(struct rafs_file_info *file);
int ram_backend_readdir(struct super_block *sb, ino_t dir_ino, struct dir_context *ctx);
void ram_backend_free_file_info(struct rafs_file_info *file_info);
int ram_backend_get_num_dir(struct super_block *sb, ino_t dir_ino);

#endif //RAFS_RAM_BACKEND_H
