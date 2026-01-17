#ifndef RAFS_NET_BACKEND_H
#define RAFS_NET_BACKEND_H

#include <linux/fs.h>
#include <linux/types.h>
#include "../../rafs.h"

// Для NET backend структуры данных не нужны - все хранится на сервере

int net_backend_init(struct super_block *sb);
void net_backend_destroy(struct super_block *sb);
struct rafs_file_info* net_backend_lookup(struct super_block *sb, ino_t parent_ino, const char *name);
struct rafs_file_info* net_backend_create(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode);
int net_backend_unlink(struct super_block *sb, ino_t parent_ino, const char *name);
struct rafs_file_info* net_backend_link(struct super_block *sb, ino_t parent_ino, const char *name, struct rafs_file_info *target);
int net_backend_is_empty_dir(struct super_block *sb, ino_t dir_ino);
ssize_t net_backend_read(struct rafs_file_info *file, char *buffer, size_t len, loff_t offset);
ssize_t net_backend_write(struct rafs_file_info *file, const char *buffer, size_t len, loff_t offset);
size_t net_backend_get_size(struct rafs_file_info *file);
int net_backend_readdir(struct super_block *sb, ino_t dir_ino, struct dir_context *ctx);
void net_backend_free_file_info(struct rafs_file_info *file_info);

#endif //RAFS_NET_BACKEND_H