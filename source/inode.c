#include <linux/string.h>
#include <linux/err.h>

#include "rafs.h"


extern struct file_operations rafs_dir_ops;
extern struct file_operations rafs_file_ops;

extern struct rafs_file* rafs_file_find(struct super_block *sb, ino_t parent_ino, const char *name);
extern struct rafs_file* rafs_file_create(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode);
extern struct rafs_file* rafs_file_link(struct super_block *sb, ino_t parent_ino, const char *name, struct rafs_file *target_file);
extern void rafs_file_delete(struct super_block *sb, ino_t parent_ino, const char *name);
extern int rafs_file_is_empty_dir(struct super_block *sb, ino_t dir_ino);


struct inode* rafs_get_inode(
    struct super_block* sb, 
    const struct inode* dir, 
    umode_t mode, 
    int i_ino
) {
    struct inode *inode = new_inode(sb);
    if (inode != NULL) {
        inode_init_owner(&init_user_ns, inode, dir, mode);
        inode->i_ino = i_ino;
        inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
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
    struct rafs_file *file;
    struct inode *inode;

    if (parent_inode == NULL || child_dentry == NULL) {
        return ERR_PTR(-EINVAL);
    }

    parent_ino = parent_inode->i_ino;
    name = child_dentry->d_name.name;
    if (name == NULL) {
        return ERR_PTR(-EINVAL);
    }

    file = rafs_file_find(parent_inode->i_sb, parent_ino, name);
    if (file == NULL) {
        return NULL;
    }

    inode = rafs_get_inode(parent_inode->i_sb, NULL, file->mode, file->ino);
    if (inode == NULL) {
        return ERR_PTR(-ENOMEM);
    }
    
    if (S_ISDIR(file->mode)) {
        inode->i_op = &rafs_inode_ops;
        inode->i_fop = &rafs_dir_ops;
        inode->i_private = file;
    } else {
        inode->i_op = &rafs_inode_ops;
        inode->i_fop = &rafs_file_ops;
        inode->i_private = file;
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
    struct rafs_file *file;
    struct inode *inode;

    file = rafs_file_create(parent_inode->i_sb, parent_ino, name, mode | S_IFREG);
    if (file == NULL) {
        return -ENOMEM;
    }

    inode = rafs_get_inode(parent_inode->i_sb, NULL, file->mode, file->ino);
    if (inode == NULL) {
        rafs_file_delete(parent_inode->i_sb, parent_ino, name);
        return -ENOMEM;
    }
    
    inode->i_op = &rafs_inode_ops;
    inode->i_fop = &rafs_file_ops;
    inode->i_private = file;

    d_add(child_dentry, inode);
    return 0;
}


int rafs_unlink(struct inode *parent_inode, struct dentry *child_dentry) {
    const char *name = child_dentry->d_name.name;
    ino_t parent_ino = parent_inode->i_ino;

    rafs_file_delete(parent_inode->i_sb, parent_ino, name);
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
    struct rafs_file *file;
    struct inode *inode;

    file = rafs_file_create(parent_inode->i_sb, parent_ino, name, mode | S_IFDIR);
    if (file == NULL) {
        return -ENOMEM;
    }

    inode = rafs_get_inode(parent_inode->i_sb, NULL, file->mode, file->ino);
    if (inode == NULL) {
        rafs_file_delete(parent_inode->i_sb, parent_ino, name);
        return -ENOMEM;
    }

    inode->i_op = &rafs_inode_ops;
    inode->i_fop = &rafs_dir_ops;

    d_add(child_dentry, inode);
    return 0;
}


int rafs_rmdir(struct inode *parent_inode, struct dentry *child_dentry) {
    const char *name = child_dentry->d_name.name;
    ino_t parent_ino = parent_inode->i_ino;
    struct rafs_file *file;
    ino_t dir_ino;

    file = rafs_file_find(parent_inode->i_sb, parent_ino, name);
    if (file == NULL) {
        return -ENOENT;
    }

    if (!S_ISDIR(file->mode)) {
        return -ENOTDIR;
    }

    dir_ino = file->ino;

    if (!rafs_file_is_empty_dir(parent_inode->i_sb, dir_ino)) {
        return -ENOTEMPTY;
    }

    rafs_file_delete(parent_inode->i_sb, parent_ino, name);
    return 0;
}


int rafs_link(struct dentry *old_dentry, struct inode *parent_dir, struct dentry *new_dentry) {
    struct inode *old_inode = old_dentry->d_inode;
    struct rafs_file *old_file;
    struct rafs_file *new_file;
    struct inode *new_inode;

    if (old_inode == NULL) {
        return -ENOENT;
    }

    old_file = (struct rafs_file *)old_inode->i_private;
    if (old_file == NULL) {
        return -ENOENT;
    }

    if (S_ISDIR(old_file->mode)) {
        return -EPERM;
    }

    new_file = rafs_file_link(parent_dir->i_sb, parent_dir->i_ino, new_dentry->d_name.name, old_file);
    if (new_file == NULL) {
        return -EEXIST;
    }

    old_file->link_count++;

    new_inode = rafs_get_inode(parent_dir->i_sb, NULL, new_file->mode, new_file->ino);
    if (new_inode == NULL) {
        rafs_file_delete(parent_dir->i_sb, parent_dir->i_ino, new_dentry->d_name.name);
        return -ENOMEM;
    }

    new_inode->i_op = &rafs_inode_ops;
    new_inode->i_fop = &rafs_file_ops;
    new_inode->i_private = old_file;

    new_inode->i_size = old_file->size;

    d_add(new_dentry, new_inode);
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
