#ifndef RAFS_RAM_BACKEND_H
#define RAFS_RAM_BACKEND_H

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include "../../rafs.h"

struct ram_file {
    struct list_head list;       // Связный список
    char name[256];              // Имя файла
    ino_t ino;                   // Номер inode
    ino_t parent_ino;            // Номер inode родительской директории
    umode_t mode;                // Тип и права доступа
    char *data;                  // Данные файла (содержимое)
    size_t size;                 // Размер данных файла
    size_t capacity;             // Емкость буфера данных
};

struct ram_sb_info {
    struct list_head file_list;  // Список файлов
    struct rw_semaphore rwsem;   // Семафор для синхронизации
    ino_t next_ino;              // Следующий свободный номер ноды
};


int ram_backend_init(struct super_block *sb);
void ram_backend_destroy(struct super_block *sb);
struct rafs_file_info* ram_backend_lookup(struct super_block *sb, ino_t parent_ino, const char *name);
struct rafs_file_info* ram_backend_create(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode);
int ram_backend_unlink(struct super_block *sb, ino_t parent_ino, const char *name);
struct rafs_file_info* ram_backend_link(struct super_block *sb, ino_t parent_ino, const char *name, struct rafs_file_info *target);
int ram_backend_is_empty_dir(struct super_block *sb, ino_t dir_ino);
ssize_t ram_backend_read(struct rafs_file_info *file, char *buffer, size_t len, loff_t offset);
ssize_t ram_backend_write(struct rafs_file_info *file, const char *buffer, size_t len, loff_t offset);
size_t ram_backend_get_size(struct rafs_file_info *file);
int ram_backend_readdir(struct super_block *sb, ino_t dir_ino, struct dir_context *ctx);
void ram_backend_free_file_info(struct rafs_file_info *file_info);

extern struct rafs_backend_ops ram_backend_ops;

#endif //RAFS_RAM_BACKEND_H
