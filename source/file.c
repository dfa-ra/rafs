#include "rafs.h"
#include "api/api.h"
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/fs.h>




ssize_t rafs_read(struct file *file, char *buffer, size_t len, loff_t *offset) {
    struct rafs_file_info *file_info;
    ssize_t bytes_read;
    char *kernel_buffer;
    size_t file_size;
    loff_t pos = *offset;

    if (file == NULL || file->f_inode == NULL) {
        return -EINVAL;
    }

    file_info = (struct rafs_file_info *)file->f_inode->i_private;
    if (file_info == NULL || S_ISDIR(file_info->mode)) {
        return -EINVAL;
    }

    file_size = rafs_backend_ops.get_size(file_info);

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

    bytes_read = rafs_backend_ops.read(file_info, kernel_buffer, len, pos);
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


ssize_t rafs_write(struct file *file, const char *buffer, size_t len, loff_t *offset) {
    struct rafs_file_info *file_info;
    char *kernel_buffer;
    ssize_t bytes_written;
    loff_t pos = *offset;

    if (file == NULL || file->f_inode == NULL) {
        return -EINVAL;
    }

    file_info = (struct rafs_file_info *)file->f_inode->i_private;
    if (file_info == NULL || S_ISDIR(file_info->mode)) {
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

    bytes_written = rafs_backend_ops.write(file_info, kernel_buffer, len, pos);
    if (bytes_written < 0) {
        kfree(kernel_buffer);
        return bytes_written;
    }

    kfree(kernel_buffer);

    *offset = pos + bytes_written;

    file->f_inode->i_size = rafs_backend_ops.get_size(file_info);
    file->f_inode->i_mtime = file->f_inode->i_ctime = current_time(file->f_inode);

    return bytes_written;
}


struct file_operations rafs_file_ops = {
    .owner = THIS_MODULE,
    .llseek = generic_file_llseek,
    .read = rafs_read,
    .write = rafs_write,
};
