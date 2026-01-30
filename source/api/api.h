#ifndef RAFS_API_H
#define RAFS_API_H

#include <linux/fs.h>
#include <linux/types.h>

// информации о файле для VFS-слоя
struct rafs_file_info {
    int ref_count;          // счетчик ссылок на эту структуру
    ino_t ino;              // inode номер
    umode_t mode;           // тип файла и права
    size_t size;            // размер файла
    struct super_block *sb; // указатель на super_block для получения токена
    void *private_data;     // указатель на внутренние данные backend
};

struct rafs_backend_ops {
    int (*init)(struct super_block *sb, const char *token);
    void (*destroy)(struct super_block *sb);

    struct rafs_file_info* (*lookup)(struct super_block *sb, ino_t parent_ino, const char *name);
    struct rafs_file_info* (*create)(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode);
    int (*unlink)(struct super_block *sb, ino_t parent_ino, const char *name);
    int (*rmdir)(struct super_block *sb, ino_t parent_ino, const char *name);
    struct rafs_file_info* (*link)(struct super_block *sb, ino_t parent_ino, const char *name, struct rafs_file_info *target);
    int (*is_empty_dir)(struct super_block *sb, ino_t dir_ino);
    int (*get_num_dir)(struct super_block *sb, ino_t dir_ino);

    ssize_t (*read)(struct rafs_file_info *file, char *buffer, size_t len, loff_t offset);
    ssize_t (*write)(struct rafs_file_info *file, const char *buffer, size_t len, loff_t offset);
    size_t (*get_size)(struct rafs_file_info *file);

    int (*readdir)(struct super_block *sb, ino_t dir_ino, struct dir_context *ctx);

    void (*free_file_info)(struct rafs_file_info *file_info);
};

extern struct rafs_backend_ops rafs_backend_ops;
 
#endif
