#include "ram_backend.h"
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "../api.h"

#define INITIAL_CAPACITY 4096

struct ram_sb_info* ram_sb_info(struct super_block *sb) {
    return (struct ram_sb_info*)sb->s_fs_info;
}

struct rafs_file_info* ram_create_file_info(struct ram_file *ram_file) {
    struct rafs_file_info *info;

    info = kmalloc(sizeof(struct rafs_file_info), GFP_KERNEL);
    if (info == NULL) {
        return NULL;
    }

    info->ref_count = 1;
    info->ino = ram_file->ino;
    info->mode = ram_file->mode;
    info->size = ram_file->size;
    info->private_data = ram_file;

    return info;
}

// Инициализация RAM backend
int ram_backend_init(struct super_block *sb) {
    struct ram_sb_info *sbi;

    sbi = kmalloc(sizeof(struct ram_sb_info), GFP_KERNEL);
    if (sbi == NULL) {
        return -ENOMEM;
    }

    INIT_LIST_HEAD(&sbi->file_list);
    init_rwsem(&sbi->rwsem);
    sbi->next_ino = 1001;

    sb->s_fs_info = sbi;
    return 0;
}

// Уничтожение RAM backend
void ram_backend_destroy(struct super_block *sb) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_file *file, *tmp;

    if (sbi == NULL) {
        return;
    }

    down_write(&sbi->rwsem);
    list_for_each_entry_safe(file, tmp, &sbi->file_list, list) {
        list_del(&file->list);
        if (file->data != NULL) {
            kfree(file->data);
        }
        kfree(file);
    }
    up_write(&sbi->rwsem);

    kfree(sbi);
    sb->s_fs_info = NULL;
}

void ram_backend_free_file_info(struct rafs_file_info *file_info) {
    if (file_info != NULL) {
        file_info->ref_count--;
        if (file_info->ref_count <= 0) {
            kfree(file_info);
        }
    }
}

struct rafs_file_info* ram_backend_lookup(struct super_block *sb, ino_t parent_ino, const char *name) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_file *file;

    if (sbi == NULL || name == NULL) {
        return NULL;
    }

    down_read(&sbi->rwsem);
    list_for_each_entry(file, &sbi->file_list, list) {
        if (file->parent_ino == parent_ino && strcmp(file->name, name) == 0) {
            up_read(&sbi->rwsem);
            return ram_create_file_info(file);
        }
    }
    up_read(&sbi->rwsem);
    return NULL;
}

struct rafs_file_info* ram_backend_create(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_file *file;

    if (sbi == NULL) {
        return NULL;
    }

    if (parent_ino == 0 && (name == NULL || *name == '\0')) {
        file = kmalloc(sizeof(struct ram_file), GFP_KERNEL);
        if (file == NULL) {
            return NULL;
        }

        file->name[0] = '\0';
        file->parent_ino = 0;
        file->mode = mode;
        file->ino = 1000;
        file->data = NULL;
        file->size = 0;
        file->capacity = 0;

        down_write(&sbi->rwsem);
        list_add_tail(&file->list, &sbi->file_list);
        up_write(&sbi->rwsem);

        return ram_create_file_info(file);
    }

    if (name == NULL) {
        return NULL;
    }

    if (ram_backend_lookup(sb, parent_ino, name) != NULL) {
        return NULL;
    }

    file = kmalloc(sizeof(struct ram_file), GFP_KERNEL);
    if (file == NULL) {
        return NULL;
    }

    strncpy(file->name, name, sizeof(file->name) - 1);
    file->name[sizeof(file->name) - 1] = '\0';
    file->parent_ino = parent_ino;
    file->mode = mode;
    file->ino = sbi->next_ino++;
    file->data = NULL;
    file->size = 0;
    file->capacity = 0;

    if (!S_ISDIR(mode)) {
        file->data = kmalloc(INITIAL_CAPACITY, GFP_KERNEL);
        if (file->data == NULL) {
            kfree(file);
            return NULL;
        }
        file->capacity = INITIAL_CAPACITY;
    }

    down_write(&sbi->rwsem);
    list_add_tail(&file->list, &sbi->file_list);
    up_write(&sbi->rwsem);

    return ram_create_file_info(file);
}

int ram_backend_unlink(struct super_block *sb, ino_t parent_ino, const char *name) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_file *file_to_delete = NULL, *file, *tmp;
    int link_count = 0;

    if (sbi == NULL || name == NULL) {
        return -EINVAL;
    }

    down_write(&sbi->rwsem);

    list_for_each_entry(file, &sbi->file_list, list) {
        if (file->parent_ino == parent_ino && strcmp(file->name, name) == 0) {
            file_to_delete = file;
            break;
        }
    }

    if (file_to_delete == NULL) {
        up_write(&sbi->rwsem);
        return -ENOENT;
    }

    list_for_each_entry(file, &sbi->file_list, list) {
        if (file->ino == file_to_delete->ino) {
            link_count++;
        }
    }

    if (link_count == 1) {
        list_for_each_entry_safe(file, tmp, &sbi->file_list, list) {
            if (file->ino == file_to_delete->ino) {
                list_del(&file->list);
                if (file->data != NULL) {
                    kfree(file->data);
                }
                kfree(file);
            }
        }
    } else {
        list_del(&file_to_delete->list);
        kfree(file_to_delete);
    }

    up_write(&sbi->rwsem);
    return 0;
}

struct rafs_file_info* ram_backend_link(struct super_block *sb, ino_t parent_ino, const char *name, struct rafs_file_info *target) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_file *file;

    if (sbi == NULL || target == NULL || name == NULL) {
        return NULL;
    }

    if (ram_backend_lookup(sb, parent_ino, name) != NULL) {
        return NULL;
    }

    file = kmalloc(sizeof(struct ram_file), GFP_KERNEL);
    if (file == NULL) {
        return NULL;
    }

    struct ram_file *target_ram = (struct ram_file *)target->private_data;

    strncpy(file->name, name, sizeof(file->name) - 1);
    file->name[sizeof(file->name) - 1] = '\0';
    file->parent_ino = parent_ino;
    file->mode = target->mode;
    file->ino = target->ino;
    file->data = target_ram->data;
    file->size = target_ram->size;
    file->capacity = target_ram->capacity;

    down_write(&sbi->rwsem);
    list_add_tail(&file->list, &sbi->file_list);
    up_write(&sbi->rwsem);

    return ram_create_file_info(file);
}

int ram_backend_is_empty_dir(struct super_block *sb, ino_t dir_ino) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_file *file;

    if (sbi == NULL) {
        return 0;
    }

    down_read(&sbi->rwsem);
    list_for_each_entry(file, &sbi->file_list, list) {
        if (file->parent_ino == dir_ino) {
            up_read(&sbi->rwsem);
            return 0;
        }
    }
    up_read(&sbi->rwsem);
    return 1;
}

ssize_t ram_backend_read(struct rafs_file_info *file_info, char *buffer, size_t len, loff_t offset) {
    struct ram_file *file;

    if (file_info == NULL || buffer == NULL) {
        return -EINVAL;
    }

    file = (struct ram_file *)file_info->private_data;
    if (file == NULL || S_ISDIR(file->mode)) {
        return -EINVAL;
    }

    if (offset >= file->size) {
        return 0;
    }

    if (len > file->size - offset) {
        len = file->size - offset;
    }

    memcpy(buffer, file->data + offset, len);
    return len;
}

ssize_t ram_backend_write(struct rafs_file_info *file_info, const char *buffer, size_t len, loff_t offset) {
    struct ram_file *file;
    size_t new_size;
    char *new_data;

    if (file_info == NULL || buffer == NULL) {
        return -EINVAL;
    }

    file = (struct ram_file *)file_info->private_data;
    if (file == NULL || S_ISDIR(file->mode)) {
        return -EINVAL;
    }

    if (len == 0) {
        return 0;
    }

    new_size = offset + len;

    if (new_size > file->capacity) {
        size_t new_capacity = file->capacity;
        while (new_capacity < new_size) {
            new_capacity *= 2;
        }

        new_data = kmalloc(new_capacity, GFP_KERNEL);
        if (new_data == NULL) {
            return -ENOMEM;
        }

        if (file->data != NULL && file->size > 0) {
            memcpy(new_data, file->data, file->size);
        }

        if (file->data != NULL) {
            kfree(file->data);
        }

        file->data = new_data;
        file->capacity = new_capacity;
    }

    memcpy(file->data + offset, buffer, len);
    file->size = new_size;

    return len;
}

size_t ram_backend_get_size(struct rafs_file_info *file_info) {
    struct ram_file *file;

    if (file_info == NULL) {
        return 0;
    }

    file = (struct ram_file *)file_info->private_data;
    if (file == NULL) {
        return 0;
    }

    return file->size;
}

int ram_backend_readdir(struct super_block *sb, ino_t dir_ino, struct dir_context *ctx) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_file *file;
    unsigned long offset = ctx->pos;
    int stored = 0;
    unsigned long file_index = 0;
    unsigned char ftype;

    if (sbi == NULL || ctx == NULL) {
        return 0;
    }

    // "." и ".."
    if (offset == 0) {
        if (!dir_emit(ctx, ".", 1, dir_ino, DT_DIR)) {
            return stored;
        }
        stored++;
        offset++;
        ctx->pos = offset;
    }

    if (offset == 1) {
        ino_t parent_ino = (dir_ino == 1000) ? 1000 : 1000;
        if (!dir_emit(ctx, "..", 2, parent_ino, DT_DIR)) {
            return stored;
        }
        stored++;
        offset++;
        ctx->pos = offset;
    }

    if (offset < 2) {
        return stored;
    }

    down_read(&sbi->rwsem);
    list_for_each_entry(file, &sbi->file_list, list) {
        if (file->parent_ino != dir_ino) {
            continue;
        }

        if (file_index < offset - 2) {
            file_index++;
            continue;
        }

        if (S_ISDIR(file->mode)) {
            ftype = DT_DIR;
        } else {
            ftype = DT_REG;
        }

        if (!dir_emit(ctx, file->name, strlen(file->name), file->ino, ftype)) {
            up_read(&sbi->rwsem);
            return stored;
        }
        stored++;
        file_index++;
        offset++;
        ctx->pos = offset;
    }
    up_read(&sbi->rwsem);

    return stored;
}

struct rafs_backend_ops ram_backend_ops = {
    .init = ram_backend_init,
    .destroy = ram_backend_destroy,
    .lookup = ram_backend_lookup,
    .create = ram_backend_create,
    .unlink = ram_backend_unlink,
    .link = ram_backend_link,
    .is_empty_dir = ram_backend_is_empty_dir,
    .read = ram_backend_read,
    .write = ram_backend_write,
    .get_size = ram_backend_get_size,
    .readdir = ram_backend_readdir,
    .free_file_info = ram_backend_free_file_info,
};
