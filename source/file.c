#include "rafs.h"
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/fs.h>

#define INITIAL_CAPACITY 4096


static struct rafs_sb_info* rafs_sb_info(struct super_block *sb) {
    return (struct rafs_sb_info*)sb->s_fs_info;
}


struct rafs_file* rafs_file_find(struct super_block *sb, ino_t parent_ino, const char *name) {
    struct rafs_sb_info *sbi = rafs_sb_info(sb);
    struct rafs_file *file;

    if (sbi == NULL) {
        return NULL;
    }

    down_read(&sbi->rwsem);
    list_for_each_entry(file, &sbi->file_list, list) {
        if (file->parent_ino == parent_ino && strcmp(file->name, name) == 0) {
            up_read(&sbi->rwsem);
            return file;
        }
    }
    up_read(&sbi->rwsem);
    return NULL;
}


struct rafs_file* rafs_file_create(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode) {
    struct rafs_sb_info *sbi = rafs_sb_info(sb);
    struct rafs_file *file;

    if (sbi == NULL) {
        return NULL;
    }

    file = rafs_file_find(sb, parent_ino, name);
    if (file != NULL) {
        return file;
    }

    file = kmalloc(sizeof(struct rafs_file), GFP_KERNEL);
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
    file->link_count = 1;

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
    
    return file;
}


void rafs_file_delete(struct super_block *sb, ino_t parent_ino, const char *name) {
    struct rafs_sb_info *sbi = rafs_sb_info(sb);
    struct rafs_file *file_to_delete = NULL, *original_file = NULL, *file, *tmp;

    if (sbi == NULL) {
        return;
    }

    down_read(&sbi->rwsem);
    list_for_each_entry(file, &sbi->file_list, list) {
        if (file->parent_ino == parent_ino && strcmp(file->name, name) == 0) {
            file_to_delete = file;
            break;
        }
    }
    up_read(&sbi->rwsem);

    if (file_to_delete == NULL) {
        return;
    }

    down_read(&sbi->rwsem);
    list_for_each_entry(file, &sbi->file_list, list) {
        if (file->ino == file_to_delete->ino) {
            original_file = file;
            break;
        }
    }
    up_read(&sbi->rwsem);

    if (original_file == NULL) {
        return;
    }

    down_write(&sbi->rwsem);
    original_file->link_count--;

    if (original_file->link_count == 0) {

        list_for_each_entry_safe(file, tmp, &sbi->file_list, list) {
            if (file->ino == file_to_delete->ino) {
                list_del(&file->list);
                kfree(file);
            }
        }

        if (original_file->data != NULL) {
            kfree(original_file->data);
        }
    } else {
        list_del(&file_to_delete->list);
        kfree(file_to_delete);
    }
    up_write(&sbi->rwsem);
}


void rafs_file_cleanup(struct super_block *sb) {
    struct rafs_sb_info *sbi = rafs_sb_info(sb);
    struct rafs_file *file, *tmp;
    
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


size_t rafs_file_get_size(struct rafs_file *file) {
    if (file == NULL) {
        return 0;
    }
    return file->size;
}


struct rafs_file* rafs_file_link(struct super_block *sb, ino_t parent_ino, const char *name, struct rafs_file *target_file) {
    struct rafs_sb_info *sbi = rafs_sb_info(sb);
    struct rafs_file *file;
    struct rafs_file *is_find_file;

    if (sbi == NULL || target_file == NULL) {
        return NULL;
    }

    is_find_file = rafs_file_find(sb, parent_ino, name);
    if (is_find_file != NULL) {
        return NULL;
    }

    file = kmalloc(sizeof(struct rafs_file), GFP_KERNEL);
    if (file == NULL) {
        return NULL;
    }

    strncpy(file->name, name, sizeof(file->name) - 1);
    file->name[sizeof(file->name) - 1] = '\0';
    file->parent_ino = parent_ino;
    file->mode = target_file->mode;
    file->ino = target_file->ino;
    file->data = target_file->data;
    file->size = target_file->size;
    file->capacity = target_file->capacity;
    file->link_count = 0;

    down_write(&sbi->rwsem);
    list_add_tail(&file->list, &sbi->file_list);
    up_write(&sbi->rwsem);

    return file;
}


int rafs_file_is_empty_dir(struct super_block *sb, ino_t dir_ino) {
    struct rafs_sb_info *sbi = rafs_sb_info(sb);
    struct rafs_file *file;

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


ssize_t rafs_read(struct file *filp, char *buffer, size_t len, loff_t *offset) {
    struct rafs_file *file;
    ssize_t bytes_read;
    char *kernel_buffer;
    size_t file_size;
    loff_t pos = *offset;

    if (filp == NULL || filp->f_inode == NULL) {
        return -EINVAL;
    }

    file = (struct rafs_file *)filp->f_inode->i_private;
    if (file == NULL || S_ISDIR(file->mode)) {
        return -EINVAL;
    }

    file_size = rafs_file_get_size(file);

    if (pos >= file_size) {
        return 0;
    }

    if (len > file_size - pos) {
        len = file_size - pos;
    }

    if (len == 0) {
        return 0;
    }

    kernel_buffer = kmalloc(len, GFP_KERNEL);
    if (kernel_buffer == NULL) {
        return -ENOMEM;
    }

    bytes_read = rafs_file_get_data(file, kernel_buffer, len, pos);
    if (bytes_read < 0) {
        kfree(kernel_buffer);
        return bytes_read;
    }

    if (copy_to_user(buffer, kernel_buffer, bytes_read)) {
        kfree(kernel_buffer);
        return -EFAULT;
    }

    kfree(kernel_buffer);

    *offset = pos + bytes_read;

    return bytes_read;

}


ssize_t rafs_write(struct file *filp, const char *buffer, size_t len, loff_t *offset) {
    struct rafs_file *file;
    char *kernel_buffer;
    char *new_data;
    size_t new_size;
    loff_t pos = *offset;

    if (filp == NULL || filp->f_inode == NULL) {
        return -EINVAL;
    }

    file = (struct rafs_file *)filp->f_inode->i_private;
    if (file == NULL || S_ISDIR(file->mode)) {
        return -EINVAL;
    }

    if (len == 0) {
        return 0;
    }

    kernel_buffer = kmalloc(len, GFP_KERNEL);
    if (kernel_buffer == NULL) {
        return -ENOMEM;
    }

    if (copy_from_user(kernel_buffer, buffer, len)) {
        kfree(kernel_buffer);
        return -EFAULT;
    }

    new_size = pos + len;

    if (new_size > file->capacity) {
        size_t new_capacity = file->capacity;
        while (new_capacity < new_size) {
            new_capacity *= 2;
        }

        new_data = kmalloc(new_capacity, GFP_KERNEL);
        if (new_data == NULL) {
            kfree(kernel_buffer);
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

    memcpy(file->data + pos, kernel_buffer, len);

    file->size = new_size;

    kfree(kernel_buffer);

    *offset = pos + len;
    filp->f_inode->i_size = file->size;
    filp->f_inode->i_mtime = filp->f_inode->i_ctime = current_time(filp->f_inode);

    return len;
}


struct file_operations rafs_file_ops = {
    .owner = THIS_MODULE,
    .llseek = generic_file_llseek,
    .read = rafs_read,
    .write = rafs_write,
};
