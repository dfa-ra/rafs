#include "rafs.h"
#include <linux/string.h>

int rafs_iterate(struct file* filp, struct dir_context* ctx) {
    struct dentry* dentry;
    struct inode* inode;
    unsigned long offset;
    int stored = 0;
    ino_t ino_val;
    struct super_block *sb;
    struct rafs_sb_info *sbi;
    struct rafs_file *file;
    unsigned char ftype;
    unsigned long file_index = 0;

    if (filp == NULL || ctx == NULL) {
        return 0;
    }

    dentry = filp->f_path.dentry;
    if (dentry == NULL) {
        return 0;
    }

    inode = dentry->d_inode;
    if (inode == NULL) {
        return 0;
    }

    offset = ctx->pos;
    ino_val = inode->i_ino;
    sb = inode->i_sb;

    sbi = (struct rafs_sb_info*)sb->s_fs_info;
    if (sbi == NULL) {
        return stored;
    }

    if (offset == 0) {
        if (!dir_emit(ctx, ".", 1, ino_val, DT_DIR)) {
            return stored;
        }
        stored++;
        offset++;
        ctx->pos = offset;
    }

    if (offset == 1) {
        if (!dir_emit(ctx, "..", 2, dentry->d_parent->d_inode->i_ino, DT_DIR)) {
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

        if (file->parent_ino != ino_val) {
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

struct file_operations rafs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = rafs_iterate,
};
