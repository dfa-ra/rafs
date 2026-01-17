#include "ram_backend.h"
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "../api.h"

#define INITIAL_CAPACITY 4096

struct ram_sb_info* ram_sb_info(struct super_block *sb) {
    return (struct ram_sb_info*)sb->s_fs_info;
}

struct rafs_file_info* ram_create_file_info(struct super_block *sb, struct ram_inode *ram_inode) {
    struct rafs_file_info *info;

    info = kmalloc(sizeof(struct rafs_file_info), GFP_KERNEL);
    if (info == NULL) {
        return NULL;
    }

    info->ref_count = 1;
    info->ino = ram_inode->ino;
    info->mode = ram_inode->mode;
    info->size = ram_inode->size;
    info->sb = sb;
    info->private_data = ram_inode;

    return info;
}

int ram_backend_init(struct super_block *sb, const char *token) {
    struct ram_sb_info *sbi;

    sbi = kmalloc(sizeof(struct ram_sb_info), GFP_KERNEL);
    if (sbi == NULL) {
        return -ENOMEM;
    }

    INIT_LIST_HEAD(&sbi->inode_list);
    INIT_LIST_HEAD(&sbi->dentry_list);
    init_rwsem(&sbi->rwsem);
    sbi->next_ino = 1001;
    sbi->token = NULL;

    sb->s_fs_info = sbi;
    return 0;
}

void ram_backend_destroy(struct super_block *sb) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_inode *inode, *inode_tmp;
    struct ram_dentry *dentry, *dentry_tmp;

    if (sbi == NULL) {
        return;
    }

    down_write(&sbi->rwsem);

    list_for_each_entry_safe(inode, inode_tmp, &sbi->inode_list, list) {
        list_del(&inode->list);
        if (inode->data != NULL) {
            kfree(inode->data);
        }
        kfree(inode);
    }

    list_for_each_entry_safe(dentry, dentry_tmp, &sbi->dentry_list, list) {
        list_del(&dentry->list);
        kfree(dentry);
    }

    up_write(&sbi->rwsem);

    if (sbi->token != NULL) {
        kfree(sbi->token);
    }
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
    struct ram_dentry *dentry;

    if (sbi == NULL || name == NULL) {
        return NULL;
    }

    down_read(&sbi->rwsem);
    list_for_each_entry(dentry, &sbi->dentry_list, list) {
        if (dentry->parent_ino == parent_ino && strcmp(dentry->name, name) == 0) {
            up_read(&sbi->rwsem);
            return ram_create_file_info(sb, dentry->inode);
        }
    }
    up_read(&sbi->rwsem);
    return NULL;
}

struct rafs_file_info* ram_backend_create(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_inode *inode;
    struct ram_dentry *dentry;

    if (sbi == NULL) {
        return NULL;
    }

    if (parent_ino == 0 && (name == NULL || *name == '\0')) {
        inode = kmalloc(sizeof(struct ram_inode), GFP_KERNEL);
        if (inode == NULL) {
            return NULL;
        }

        inode->ino = 1000;
        inode->mode = mode;
        inode->data = NULL;
        inode->size = 0;
        inode->capacity = 0;
        inode->nlink = 1;

        dentry = kmalloc(sizeof(struct ram_dentry), GFP_KERNEL);
        if (dentry == NULL) {
            kfree(inode);
            return NULL;
        }

        dentry->parent_ino = 0;
        dentry->name[0] = '\0';
        dentry->inode = inode;

        down_write(&sbi->rwsem);
        list_add_tail(&inode->list, &sbi->inode_list);
        list_add_tail(&dentry->list, &sbi->dentry_list);
        up_write(&sbi->rwsem);

        return ram_create_file_info(sb, inode);
    }

    if (name == NULL) {
        return NULL;
    }

    if (ram_backend_lookup(sb, parent_ino, name) != NULL) {
        return NULL;
    }

    inode = kmalloc(sizeof(struct ram_inode), GFP_KERNEL);
    if (inode == NULL) {
        return NULL;
    }

    inode->ino = sbi->next_ino++;
    inode->mode = mode;
    inode->data = NULL;
    inode->size = 0;
    inode->capacity = 0;
    inode->nlink = 1;

    if (!S_ISDIR(mode)) {
        inode->data = kmalloc(INITIAL_CAPACITY, GFP_KERNEL);
        if (inode->data == NULL) {
            kfree(inode);
            return NULL;
        }
        inode->capacity = INITIAL_CAPACITY;
    }

    dentry = kmalloc(sizeof(struct ram_dentry), GFP_KERNEL);
    if (dentry == NULL) {
        if (inode->data != NULL) {
            kfree(inode->data);
        }
        kfree(inode);
        return NULL;
    }

    strncpy(dentry->name, name, sizeof(dentry->name) - 1);
    dentry->name[sizeof(dentry->name) - 1] = '\0';
    dentry->parent_ino = parent_ino;
    dentry->inode = inode;

    down_write(&sbi->rwsem);
    list_add_tail(&inode->list, &sbi->inode_list);
    list_add_tail(&dentry->list, &sbi->dentry_list);
    up_write(&sbi->rwsem);

    return ram_create_file_info(sb, inode);
}

int ram_backend_unlink(struct super_block *sb, ino_t parent_ino, const char *name) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_dentry *dentry_to_delete = NULL;
    struct ram_inode *inode;

    if (sbi == NULL || name == NULL) {
        return -EINVAL;
    }

    down_write(&sbi->rwsem);

    list_for_each_entry(dentry_to_delete, &sbi->dentry_list, list) {
        if (dentry_to_delete->parent_ino == parent_ino && strcmp(dentry_to_delete->name, name) == 0) {
            break;
        }
    }

    if (dentry_to_delete == NULL) {
        up_write(&sbi->rwsem);
        return -ENOENT;
    }

    inode = dentry_to_delete->inode;

    list_del(&dentry_to_delete->list);
    kfree(dentry_to_delete);

    inode->nlink--;

    if (inode->nlink == 0) {
        list_del(&inode->list);
        if (inode->data != NULL) {
            kfree(inode->data);
        }
        kfree(inode);
    }

    up_write(&sbi->rwsem);
    return 0;
}

struct rafs_file_info* ram_backend_link(struct super_block *sb, ino_t parent_ino, const char *name, struct rafs_file_info *target) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_inode *target_inode;
    struct ram_dentry *dentry;

    if (sbi == NULL || target == NULL || name == NULL) {
        return NULL;
    }

    if (ram_backend_lookup(sb, parent_ino, name) != NULL) {
        return NULL;
    }

    target_inode = (struct ram_inode *)target->private_data;
    if (target_inode == NULL) {
        return NULL;
    }

    dentry = kmalloc(sizeof(struct ram_dentry), GFP_KERNEL);
    if (dentry == NULL) {
        return NULL;
    }

    strncpy(dentry->name, name, sizeof(dentry->name) - 1);
    dentry->name[sizeof(dentry->name) - 1] = '\0';
    dentry->parent_ino = parent_ino;
    dentry->inode = target_inode;

    down_write(&sbi->rwsem);
    target_inode->nlink++;
    list_add_tail(&dentry->list, &sbi->dentry_list);
    up_write(&sbi->rwsem);

    return ram_create_file_info(sb, target_inode);
}

int ram_backend_is_empty_dir(struct super_block *sb, ino_t dir_ino) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_dentry *dentry;

    if (sbi == NULL) {
        return 0;
    }

    down_read(&sbi->rwsem);
    list_for_each_entry(dentry, &sbi->dentry_list, list) {
        if (dentry->parent_ino == dir_ino) {
            up_read(&sbi->rwsem);
            return 0;
        }
    }
    up_read(&sbi->rwsem);
    return 1;
}

ssize_t ram_backend_read(struct rafs_file_info *file_info, char *buffer, size_t len, loff_t offset) {
    struct ram_inode *inode;

    if (file_info == NULL || buffer == NULL) {
        return -EINVAL;
    }

    inode = (struct ram_inode *)file_info->private_data;
    if (inode == NULL || S_ISDIR(inode->mode)) {
        return -EINVAL;
    }

    if (offset >= inode->size) {
        return 0;
    }

    if (len > inode->size - offset) {
        len = inode->size - offset;
    }

    memcpy(buffer, inode->data + offset, len);
    return len;
}

ssize_t ram_backend_write(struct rafs_file_info *file_info, const char *buffer, size_t len, loff_t offset) {
    struct ram_inode *inode;
    size_t new_size;
    char *new_data;

    if (file_info == NULL || buffer == NULL) {
        return -EINVAL;
    }

    inode = (struct ram_inode *)file_info->private_data;
    if (inode == NULL || S_ISDIR(inode->mode)) {
        return -EINVAL;
    }

    if (len == 0) {
        return 0;
    }

    new_size = offset + len;

    if (new_size > inode->capacity) {
        size_t new_capacity = inode->capacity > 0 ? inode->capacity : INITIAL_CAPACITY;
        while (new_capacity < new_size) {
            new_capacity *= 2;
        }

        new_data = kmalloc(new_capacity, GFP_KERNEL);
        if (new_data == NULL) {
            return -ENOMEM;
        }

        if (inode->data != NULL && inode->size > 0) {
            memcpy(new_data, inode->data, inode->size);
        }

        if (inode->data != NULL) {
            kfree(inode->data);
        }

        inode->data = new_data;
        inode->capacity = new_capacity;
    }

    memcpy(inode->data + offset, buffer, len);
    inode->size = new_size;

    return len;
}

size_t ram_backend_get_size(struct rafs_file_info *file_info) {
    struct ram_inode *inode;

    if (file_info == NULL) {
        return 0;
    }

    inode = (struct ram_inode *)file_info->private_data;
    if (inode == NULL) {
        return 0;
    }

    return inode->size;
}

int ram_backend_readdir(struct super_block *sb, ino_t dir_ino, struct dir_context *ctx) {
    struct ram_sb_info *sbi = ram_sb_info(sb);
    struct ram_dentry *dentry;
    unsigned long offset = ctx->pos;
    int stored = 0;
    unsigned long file_index = 0;
    unsigned char ftype;

    if (sbi == NULL || ctx == NULL) {
        return 0;
    }

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
    list_for_each_entry(dentry, &sbi->dentry_list, list) {
        if (dentry->parent_ino != dir_ino) {
            continue;
        }

        if (file_index < offset - 2) {
            file_index++;
            continue;
        }

        if (S_ISDIR(dentry->inode->mode)) {
            ftype = DT_DIR;
        } else {
            ftype = DT_REG;
        }

        if (!dir_emit(ctx, dentry->name, strlen(dentry->name), dentry->inode->ino, ftype)) {
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
