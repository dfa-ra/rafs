#include <linux/string.h>
#include <linux/err.h>

#include "rafs.h"
#include "api/api.h"

extern struct file_operations rafs_dir_ops;
extern struct file_operations rafs_file_ops;


struct inode *rafs_get_inode(
        struct super_block *sb,
        const struct inode *dir,
        umode_t mode,
        int ino,
        int ref_count
) {
    struct inode *inode = iget_locked(sb, ino);
    if (!inode)
        return NULL;

    if (inode->i_state & I_NEW) {
        inode_init_owner(&init_user_ns, inode, dir, mode);
        inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
        set_nlink(inode, ref_count);
        unlock_new_inode(inode);
    }

    return inode;
}


struct dentry* rafs_lookup(
    struct inode* parent_inode,
    struct dentry* child_dentry,
    unsigned int flag
) {
    ino_t parent_ino;
    const char *name;
    struct rafs_file_info *file_info;
    struct inode *inode;

    if (parent_inode == NULL || child_dentry == NULL) {
        return ERR_PTR(-EINVAL);
    }

    parent_ino = parent_inode->i_ino;
    name = child_dentry->d_name.name;
    if (name == NULL) {
        return ERR_PTR(-EINVAL);
    }

    file_info = rafs_backend_ops.lookup(parent_inode->i_sb, parent_ino, name);
    if (file_info == NULL) {
        return NULL;
    }

    inode = rafs_get_inode(parent_inode->i_sb, NULL, file_info->mode, file_info->ino, file_info->ref_count);
    if (inode == NULL) {
        rafs_backend_ops.free_file_info(file_info);
        return ERR_PTR(-ENOMEM);
    }

    if (S_ISDIR(file_info->mode)) {
        inode->i_op = &rafs_inode_ops;
        inode->i_fop = &rafs_dir_ops;
        inode->i_private = file_info;
    } else {
        inode->i_op = &rafs_inode_ops;
        inode->i_fop = &rafs_file_ops;
        inode->i_private = file_info;
    }

    d_add(child_dentry, inode);
    return NULL;
}


int rafs_create(
    struct user_namespace *mnt_userns,
    struct inode *parent_inode,
    struct dentry *child_dentry,
    umode_t mode,
    bool b
) {
    ino_t parent_ino = parent_inode->i_ino;
    const char *name = child_dentry->d_name.name;
    struct rafs_file_info *file_info;
    struct inode *inode;

    file_info = rafs_backend_ops.create(parent_inode->i_sb, parent_ino, name, mode | S_IFREG);
    if (file_info == NULL) {
        return -EEXIST;
    }

    inode = rafs_get_inode(parent_inode->i_sb, NULL, file_info->mode, file_info->ino, file_info->ref_count);
    if (inode == NULL) {
        rafs_backend_ops.unlink(parent_inode->i_sb, parent_ino, name);
        rafs_backend_ops.free_file_info(file_info);
        return -ENOMEM;
    }

    inode->i_op = &rafs_inode_ops;
    inode->i_fop = &rafs_file_ops;
    inode->i_private = file_info;

    d_add(child_dentry, inode);
    return 0;
}


int rafs_unlink(struct inode *parent_inode, struct dentry *child_dentry)
{
    const char *name = child_dentry->d_name.name;
    ino_t parent_ino = parent_inode->i_ino;
    struct inode *inode = d_inode(child_dentry);
    int ret;

    if (S_ISDIR(inode->i_mode)) {
        return -EISDIR;
    }

    ret = rafs_backend_ops.unlink(parent_inode->i_sb, parent_ino, name);
    if (ret)
      return ret;

    drop_nlink(inode);
    return 0;
}



int rafs_mkdir(
    struct user_namespace *mnt_userns,
    struct inode *parent_inode,
    struct dentry *child_dentry,
    umode_t mode
) {
    ino_t parent_ino = parent_inode->i_ino;
    const char *name = child_dentry->d_name.name;
    struct rafs_file_info *file_info;
    struct inode *inode;

    file_info = rafs_backend_ops.create(parent_inode->i_sb, parent_ino, name, mode | S_IFDIR);
    if (file_info == NULL) {
        return -EEXIST;
    }

    inode = rafs_get_inode(parent_inode->i_sb, NULL, file_info->mode, file_info->ino, 2);
    if (inode == NULL) {
        rafs_backend_ops.unlink(parent_inode->i_sb, parent_ino, name);
        rafs_backend_ops.free_file_info(file_info);
        return -ENOMEM;
    }

    inode->i_op = &rafs_inode_ops;
    inode->i_fop = &rafs_dir_ops;
    inode->i_private = file_info;

    inc_nlink(parent_inode);

    d_add(child_dentry, inode);
    return 0;
}


int rafs_rmdir(struct inode *parent_inode, struct dentry *child_dentry) {
    const char *name = child_dentry->d_name.name;
    ino_t parent_ino = parent_inode->i_ino;
    struct rafs_file_info *file_info;
    struct inode *inode;
    int ret;

    inode = d_inode(child_dentry);
    if (inode == NULL) {
        return -ENOENT;
    }

    if (!S_ISDIR(inode->i_mode)) {
        return -ENOTDIR;
    }

    file_info = rafs_backend_ops.lookup(parent_inode->i_sb, parent_ino, name);
    if (file_info == NULL) {
        return -ENOENT;
    }

    if (!S_ISDIR(file_info->mode)) {
        rafs_backend_ops.free_file_info(file_info);
        return -ENOTDIR;
    }

    if (!rafs_backend_ops.is_empty_dir(parent_inode->i_sb, file_info->ino)) {
        rafs_backend_ops.free_file_info(file_info);
        return -ENOTEMPTY;
    }

    drop_nlink(inode);
    drop_nlink(parent_inode);

    ret = rafs_backend_ops.rmdir(parent_inode->i_sb, parent_ino, name);
    rafs_backend_ops.free_file_info(file_info);
    if (ret) {
        inc_nlink(inode);
        inc_nlink(parent_inode);
        return ret;
    }

    return 0;
}


int rafs_link(struct dentry *old_dentry, struct inode *parent_dir, struct dentry *new_dentry) {
    struct inode *old_inode = old_dentry->d_inode;
    struct rafs_file_info *old_file_info;
    struct rafs_file_info *new_file_info;
    struct inode *new_inode;

    if (old_inode == NULL) {
        return -ENOENT;
    }

    old_file_info = (struct rafs_file_info *)old_inode->i_private;
    if (old_file_info == NULL) {
        return -ENOENT;
    }

    if (S_ISDIR(old_file_info->mode)) {
        return -EPERM;
    }

    new_file_info = rafs_backend_ops.link(parent_dir->i_sb, parent_dir->i_ino, new_dentry->d_name.name, old_file_info);
    if (new_file_info == NULL) {
        return -EEXIST;
    }

    new_inode = rafs_get_inode(parent_dir->i_sb, NULL, new_file_info->mode, new_file_info->ino, new_file_info->ref_count);
    if (new_inode == NULL) {
        rafs_backend_ops.unlink(parent_dir->i_sb, parent_dir->i_ino, new_dentry->d_name.name);
        rafs_backend_ops.free_file_info(new_file_info);
        return -ENOMEM;
    }

    inc_nlink(old_inode);
    d_instantiate(new_dentry, old_inode);

    return 0;
}


struct inode_operations rafs_inode_ops = {
    .lookup = rafs_lookup,
    .create = rafs_create,
    .unlink = rafs_unlink,
    .mkdir = rafs_mkdir,
    .rmdir = rafs_rmdir,
    .link = rafs_link,
};

