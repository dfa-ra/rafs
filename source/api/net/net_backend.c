#include "net_backend.h"
#include "http.h"
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "../api.h"

#define NET_LOG(fmt, ...) pr_info("[rafs-net] " fmt, ##__VA_ARGS__)

struct net_sb_info* net_sb_info(struct super_block *sb) {
    return (struct net_sb_info*)sb->s_fs_info;
}

struct lookup_response {
    uint32_t ino;      // I
    uint32_t mode;     // I
    uint64_t size;     // Q
    uint32_t nlink;     // I
};

struct readdir_entry {
    uint32_t ino;      // I
    char name[256];    // 256 s
    uint32_t mode;     // I
};

static const char* get_token_from_file_info(struct rafs_file_info *file) {
    struct net_sb_info *sbi;
    if (file != NULL && file->sb != NULL && (sbi = net_sb_info(file->sb)) != NULL) {
        return sbi->token;
    }
    return "default_token";
}


static const char* get_token(struct super_block *sb) {
    struct net_sb_info *sbi;
    if (sb != NULL && (sbi = net_sb_info(sb)) != NULL) {
        return sbi->token;
    }
    return "default_token";
}

static struct rafs_file_info* net_create_file_info(struct super_block *sb, ino_t ino, umode_t mode, size_t size, int nlink) {
    struct rafs_file_info *info;

    info = kmalloc(sizeof(struct rafs_file_info), GFP_KERNEL);
    if (info == NULL) {
        return NULL;
    }

    info->ref_count = nlink;
    info->ino = ino;
    info->mode = mode;
    info->size = size;
    info->sb = sb;
    info->private_data = NULL;

    return info;
}

static char* encode_string(const char *str) {
    size_t len = strlen(str);
    NET_LOG("encode_string: input='%s', len=%zu\n", str, len);
    char *encoded = kmalloc(len * 3 + 1, GFP_KERNEL);
    if (encoded == NULL) {
        NET_LOG("encode_string: kmalloc failed\n");
        return NULL;
    }
    encode(str, encoded);
    NET_LOG("encode_string: output='%s'\n", encoded);
    return encoded;
}

int net_backend_init(struct super_block *sb, const char *token) {
    struct net_sb_info *sbi;

    NET_LOG("init called with token: %s\n", token ? token : "NULL");

    sbi = kmalloc(sizeof(struct net_sb_info), GFP_KERNEL);
    if (sbi == NULL) {
        NET_LOG("init: failed to allocate sb_info\n");
        return -ENOMEM;
    }

    if (token != NULL) {
        sbi->token = kstrdup(token, GFP_KERNEL);
        if (sbi->token == NULL) {
            kfree(sbi);
            NET_LOG("init: failed to duplicate token\n");
            return -ENOMEM;
        }
    } else {
        sbi->token = kstrdup("default_token", GFP_KERNEL);
        if (sbi->token == NULL) {
            kfree(sbi);
            NET_LOG("init: failed to duplicate default token\n");
            return -ENOMEM;
        }
    }

    sb->s_fs_info = sbi;
    NET_LOG("init: success\n");
    return 0;
}

void net_backend_destroy(struct super_block *sb) {
    struct net_sb_info *sbi = net_sb_info(sb);

    NET_LOG("destroy called\n");

    if (sbi != NULL) {
        if (sbi->token != NULL) {
            kfree(sbi->token);
        }
        kfree(sbi);
        sb->s_fs_info = NULL;
    }
}

void net_backend_free_file_info(struct rafs_file_info *file_info) {
    NET_LOG("free_file_info called, ref_count=%d\n", file_info ? file_info->ref_count : -1);
    if (file_info != NULL) {
        file_info->ref_count--;
        if (file_info->ref_count <= 0) {
            kfree(file_info);
        }
    }
}

size_t net_backend_get_size(struct rafs_file_info *file) {
    return file->size;
}

struct rafs_file_info* net_backend_lookup(struct super_block *sb, ino_t parent_ino, const char *name) {
    char *encoded_name;
    struct lookup_response response;
    int64_t result;

    NET_LOG("lookup called: parent_ino=%lu, name='%s'\n", parent_ino, name ? name : "NULL");

    if (name == NULL) {
        NET_LOG("lookup: name is NULL\n");
        return NULL;
    }

    encoded_name = encode_string(name);
    if (encoded_name == NULL) {
        NET_LOG("lookup: failed to encode name\n");
        return NULL;
    }
    NET_LOG("lookup: encoded_name='%s'\n", encoded_name);

    NET_LOG("lookup: calling http_call\n");
    char parent_str[32], name_param[256+10];
    char *params[2];
    snprintf(parent_str, sizeof(parent_str), "parent_id=%lu", parent_ino);
    snprintf(name_param, sizeof(name_param), "name=%s", encoded_name);
    params[0] = parent_str;
    params[1] = name_param;
    NET_LOG("lookup: params[0]='%s', params[1]='%s'\n", params[0], params[1]);
    result = rafs_http_call(get_token(sb), "lookup", (char*)&response, 20, 2,
                           params[0], params[1]);

    kfree(encoded_name);

    if (result < 0) {
        NET_LOG("lookup: http_call failed with %lld\n", result);
        return NULL;
    }

    NET_LOG("lookup: success, ino=%u, mode=%u, size=%llu, mode=%u\n", response.ino, response.mode, response.size, response.nlink);
    return net_create_file_info(sb, response.ino, response.mode, response.size, response.nlink);
}

struct rafs_file_info* net_backend_create(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode) {
    NET_LOG("create called: parent_ino=%lu, name='%s', mode=%u\n", parent_ino, name ? name : "NULL", mode);

    if (parent_ino == 0 && (name == NULL || *name == '\0')) {
        NET_LOG("create: root directory case\n");
        return net_create_file_info(sb, 1000, mode, 0, 1);
    }

    char *encoded_name;
    struct lookup_response response;
    int64_t result;

    if (name == NULL) {
        NET_LOG("create: name is NULL\n");
        return NULL;
    }

    encoded_name = encode_string(name);
    if (encoded_name == NULL) {
        NET_LOG("create: failed to encode name\n");
        return NULL;
    }

    NET_LOG("create: calling http_call\n");
    char parent_str[32], name_param[256+10], mode_str[32];
    snprintf(parent_str, sizeof(parent_str), "parent_id=%lu", parent_ino);
    snprintf(name_param, sizeof(name_param), "name=%s", encoded_name);
    snprintf(mode_str, sizeof(mode_str), "mode=%u", mode);
    result = rafs_http_call(get_token(sb), "create", (char*)&response, sizeof(response), 3,
                           parent_str, name_param, mode_str);

    kfree(encoded_name);

    if (result < 0) {
        NET_LOG("create: http_call failed with %lld\n", result);
        return NULL;
    }

    NET_LOG("create: success, ino=%u, mode=%u, size=%llu\n", response.ino, response.mode, response.size);
    return net_create_file_info(sb, response.ino, response.mode, response.size, 1);
}


int net_backend_unlink(struct super_block *sb, ino_t parent_ino, const char *name) {
    char *encoded_name;
    int64_t result;

    if (name == NULL) {
        return -EINVAL;
    }

    encoded_name = encode_string(name);
    if (encoded_name == NULL) {
        return -ENOMEM;
    }

    char parent_str[32], name_param[256+10];
    snprintf(parent_str, sizeof(parent_str), "parent_id=%lu", parent_ino);
    snprintf(name_param, sizeof(name_param), "name=%s", encoded_name);
    result = rafs_http_call(get_token(sb), "unlink", NULL, 0, 2,
                           parent_str, name_param);

    kfree(encoded_name);

    return result < 0 ? result : 0;
}

int net_backend_rmdir(struct super_block *sb, ino_t parent_ino, const char *name) {
    char *encoded_name;
    int64_t result;

    if (name == NULL) {
        return -EINVAL;
    }

    encoded_name = encode_string(name);
    if (encoded_name == NULL) {
        return -ENOMEM;
    }

    char parent_str[32], name_param[256+10];
    snprintf(parent_str, sizeof(parent_str), "parent_id=%lu", parent_ino);
    snprintf(name_param, sizeof(name_param), "name=%s", encoded_name);
    result = rafs_http_call(get_token(sb), "rmdir", NULL, 0, 2,
                           parent_str, name_param);

    kfree(encoded_name);

    return result < 0 ? result : 0;
}

struct rafs_file_info* net_backend_link(struct super_block *sb, ino_t parent_ino, const char *name, struct rafs_file_info *target) {
    char *encoded_name;
    int64_t result;

    NET_LOG("link: parent_ino=%lu, name='%s', target_ino=%lu\n", parent_ino, name ? name : "NULL", target ? target->ino : 0);

    if (name == NULL || target == NULL) {
        return NULL;
    }

    encoded_name = encode_string(name);
    if (encoded_name == NULL) {
        NET_LOG("link: failed to encode name\n");
        return NULL;
    }

    char parent_str[32], name_param[256+10], target_str[32];
    char *params[3];

    snprintf(parent_str, sizeof(parent_str), "parent_id=%lu", parent_ino);
    snprintf(name_param, sizeof(name_param), "name=%s", encoded_name);
    snprintf(target_str, sizeof(target_str), "target_id=%lu", target->ino);

    params[0] = parent_str;
    params[1] = name_param;
    params[2] = target_str;

    NET_LOG("link: params[0]='%s', params[1]='%s', params[2]='%s'\n", params[0], params[1], params[2]);

    result = rafs_http_call(get_token(sb), "link", NULL, 0, 3,
                           params[0], params[1], params[2]);

    kfree(encoded_name);

    if (result < 0) {
        NET_LOG("link: http_call failed with %lld\n", result);
        return NULL;
    }

    NET_LOG("link: success\n");

    target->ref_count++;
    return target;
}

int net_backend_is_empty_dir(struct super_block *sb, ino_t dir_ino) {
    int64_t result;

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "id=%lu", dir_ino);
    result = rafs_http_call(get_token(sb), "is_empty_dir", NULL, 0, 1,
                           id_str);

    return result < 0 ? result : (result == 0 ? 1 : 0);
}

int net_backend_get_num_dir(struct super_block *sb, ino_t dir_ino)
{
    int64_t result;
    uint32_t response = 0;
    char id_str[32];

    snprintf(id_str, sizeof(id_str), "id=%lu", dir_ino);

    result = rafs_http_call(get_token(sb), "get_num_dir", (char *)&response, sizeof(response), 1, id_str);

    if (result < 0)
      return (int)result;
    response = le32_to_cpu(response);

    return (int)response;
}


ssize_t net_backend_read(struct rafs_file_info *file, char *buffer, size_t len, loff_t offset) {
    char *response_buffer;
    int64_t result;

    NET_LOG("read: ino=%lu, len=%zu, offset=%llu\n", file->ino, len, offset);

    if (file == NULL || buffer == NULL) {
        return -EINVAL;
    }

    response_buffer = kmalloc(len, GFP_KERNEL);
    if (response_buffer == NULL) {
        return -ENOMEM;
    }

    char id_str[32], offset_str[32], size_str[32];
    snprintf(id_str, sizeof(id_str), "id=%lu", file->ino);
    snprintf(offset_str, sizeof(offset_str), "offset=%llu", offset);
    snprintf(size_str, sizeof(size_str), "size=%zu", len);
    result = rafs_http_call(get_token_from_file_info(file), "read", response_buffer, len, 3,
                           id_str, offset_str, size_str);

    if (result < 0) {
        NET_LOG("read: http_call failed with %lld\n", result);
        kfree(response_buffer);
        return result;
    }

    size_t bytes_read = result;
    if (bytes_read > len) {
        bytes_read = len;
    }

    NET_LOG("read: success, bytes_read=%zu\n", bytes_read);
    memcpy(buffer, response_buffer, bytes_read);
    kfree(response_buffer);

    return bytes_read;
}

ssize_t net_backend_write(struct rafs_file_info *file, const char *buffer, size_t len, loff_t offset) {
    char *encoded_data;
    int64_t result;

    NET_LOG("write: ino=%lu, len=%zu, offset=%llu\n", file->ino, len, offset);

    if (file == NULL || buffer == NULL) {
        return -EINVAL;
    }

    encoded_data = kmalloc(len * 3 + 1, GFP_KERNEL);
    if (encoded_data == NULL) {
        return -ENOMEM;
    }

    encode(buffer, encoded_data);

    NET_LOG("write: encoded_data length=%zu\n", strlen(encoded_data));

    char id_str[32], offset_str[32], buf_param[256+10];
    char *params[3];

    snprintf(id_str, sizeof(id_str), "id=%lu", file->ino);
    snprintf(offset_str, sizeof(offset_str), "offset=%llu", offset);
    snprintf(buf_param, sizeof(buf_param), "buf=%s", encoded_data);

    params[0] = id_str;
    params[1] = offset_str;
    params[2] = buf_param;

    NET_LOG("write: params[0]='%s', params[1]='%s', params[2]='%s'\n", params[0], params[1], params[2]);

    result = rafs_http_call(get_token_from_file_info(file), "write", NULL, 0, 3,
                           params[0], params[1], params[2]);

    kfree(encoded_data);

    if (result < 0) {
        NET_LOG("write: http_call failed with %lld\n", result);
        return result;
    }

    file->size = offset + len;

    NET_LOG("write: success, bytes_written=%zu, new_size=%zu\n", len, file->size);
    return len;
}

int net_backend_readdir(struct super_block *sb, ino_t dir_ino, struct dir_context *ctx) {
    struct readdir_entry entry;
    int64_t result;
    loff_t offset = ctx->pos;

    NET_LOG("readdir: dir_ino=%lu, ctx->pos=%llu\n", dir_ino, ctx->pos);

    while (1) {
        char id_str[32], offset_str[32];
        snprintf(id_str, sizeof(id_str), "id=%lu", dir_ino);
        snprintf(offset_str, sizeof(offset_str), "offset=%llu", offset);

        result = rafs_http_call(get_token(sb), "iterate", (char*)&entry, sizeof(entry), 2,
                               id_str, offset_str);

        NET_LOG("readdir: iterate result=%lld, offset=%llu\n", result, offset);

        if (result < 0) {
            NET_LOG("readdir: no more entries (result=%lld)\n", result);
            break;
        }

        entry.name[255] = '\0';
        char *name_end = memchr(entry.name, '\0', 256);
        if (name_end) {
            *name_end = '\0';
        }

        NET_LOG("readdir: entry ino=%u, name='%s', mode=%u\n", entry.ino, entry.name, entry.mode);

        int emit_result = dir_emit(ctx, entry.name, strlen(entry.name),
                      entry.ino, entry.mode >> 12);
        NET_LOG("readdir: dir_emit returned %d\n", emit_result);

        if (!emit_result) {
            NET_LOG("readdir: dir_emit stopped iteration\n");
            break;
        }

        ctx->pos = ++offset;
    }

    NET_LOG("readdir: finished\n");
    return 0;
}

