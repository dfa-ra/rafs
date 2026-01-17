#include "net_backend.h"
#include "http.h"
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "../api.h"

#define NET_LOG(fmt, ...) pr_info("[rafs-net] " fmt, ##__VA_ARGS__)

// Структуры для ответов сервера (соответствует pack_stat в backend)
struct lookup_response {
    uint32_t ino;      // 4 байта
    uint32_t mode;     // 4 байта
    uint64_t size;     // 8 байт
    // Итого: 16 байт, соответствует "<IIQ" в Python
};

struct readdir_entry {
    uint32_t ino;      // 4 байта
    char name[256];    // 256 байт
    uint32_t mode;     // 4 байта
    // Итого: 264 байта, соответствует "<I256sI" в Python
};

#define TOKEN "default_token"

// Вспомогательная функция для создания rafs_file_info
static struct rafs_file_info* net_create_file_info(ino_t ino, umode_t mode, size_t size) {
    struct rafs_file_info *info;

    info = kmalloc(sizeof(struct rafs_file_info), GFP_KERNEL);
    if (info == NULL) {
        return NULL;
    }

    info->ref_count = 1;
    info->ino = ino;
    info->mode = mode;
    info->size = size;
    info->private_data = NULL;  // NET backend не хранит private data

    return info;
}

// Вспомогательная функция для кодирования строки
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

// Инициализация NET backend (не требуется - без состояния)
int net_backend_init(struct super_block *sb) {
    NET_LOG("init called\n");
    return 0;
}

// Уничтожение NET backend (не требуется - без состояния)
void net_backend_destroy(struct super_block *sb) {
    NET_LOG("destroy called\n");
    // Ничего не делаем
}

// Освобождение структуры rafs_file_info
void net_backend_free_file_info(struct rafs_file_info *file_info) {
    NET_LOG("free_file_info called, ref_count=%d\n", file_info ? file_info->ref_count : -1);
    if (file_info != NULL) {
        file_info->ref_count--;
        if (file_info->ref_count <= 0) {
            kfree(file_info);
        }
    }
}

// Получение размера файла
size_t net_backend_get_size(struct rafs_file_info *file) {
    return file->size;
}

// Поиск файла/директории
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
    // Сервер возвращает 16 байт: ino(4), mode(4), size(8)
    char parent_str[32], name_param[256+10];
    char *params[2];
    snprintf(parent_str, sizeof(parent_str), "parent_id=%lu", parent_ino);
    snprintf(name_param, sizeof(name_param), "name=%s", encoded_name);
    params[0] = parent_str;
    params[1] = name_param;
    NET_LOG("lookup: params[0]='%s', params[1]='%s'\n", params[0], params[1]);
    result = rafs_http_call(TOKEN, "lookup", (char*)&response, 16, 2,
                           params[0], params[1]);

    kfree(encoded_name);

    if (result < 0) {
        NET_LOG("lookup: http_call failed with %lld\n", result);
        return NULL;
    }

    NET_LOG("lookup: success, ino=%u, mode=%u, size=%llu\n", response.ino, response.mode, response.size);
    return net_create_file_info(response.ino, response.mode, response.size);
}

// Создание файла
struct rafs_file_info* net_backend_create(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode) {
    NET_LOG("create called: parent_ino=%lu, name='%s', mode=%u\n", parent_ino, name ? name : "NULL", mode);

    // Специальный случай для корневой директории
    if (parent_ino == 0 && (name == NULL || *name == '\0')) {
        NET_LOG("create: root directory case\n");
        return net_create_file_info(1000, mode, 0);
    }

    char *encoded_name;
    ino_t new_ino;
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
    uint32_t new_inode_id;
    snprintf(parent_str, sizeof(parent_str), "parent_id=%lu", parent_ino);
    snprintf(name_param, sizeof(name_param), "name=%s", encoded_name);
    snprintf(mode_str, sizeof(mode_str), "mode=%u", mode);
    result = rafs_http_call(TOKEN, "create", (char*)&new_inode_id, sizeof(new_inode_id), 3,
                           parent_str, name_param, mode_str);

    kfree(encoded_name);

    if (result < 0) {
        NET_LOG("create: http_call failed with %lld\n", result);
        return NULL;
    }

    NET_LOG("create: success, new_inode_id=%u\n", new_inode_id);
    // Сервер возвращает только inode number, предполагаем size=0 для нового файла
    return net_create_file_info(new_inode_id, mode, 0);
}

// Удаление файла/директории
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
    result = rafs_http_call(TOKEN, "unlink", NULL, 0, 2,
                           parent_str, name_param);

    kfree(encoded_name);

    return result < 0 ? result : 0;
}

// Создание hard link
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

    result = rafs_http_call(TOKEN, "link", NULL, 0, 3,
                           params[0], params[1], params[2]);

    kfree(encoded_name);

    if (result < 0) {
        NET_LOG("link: http_call failed with %lld\n", result);
        return NULL;
    }

    NET_LOG("link: success\n");
    // Возвращаем копию target file_info с увеличенным ref_count
    target->ref_count++;
    return target;
}

// Проверка, является ли директория пустой
int net_backend_is_empty_dir(struct super_block *sb, ino_t dir_ino) {
    int64_t result;

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "id=%lu", dir_ino);
    result = rafs_http_call(TOKEN, "is_empty_dir", NULL, 0, 1,
                           id_str);

    return result < 0 ? result : (result == 0 ? 1 : 0);  // result == 0 значит пустая
}

// Чтение данных из файла
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
    result = rafs_http_call(TOKEN, "read", response_buffer, len, 3,
                           id_str, offset_str, size_str);

    if (result < 0) {
        NET_LOG("read: http_call failed with %lld\n", result);
        kfree(response_buffer);
        return result;
    }

    // result содержит количество прочитанных байт
    size_t bytes_read = result;
    if (bytes_read > len) {
        bytes_read = len;
    }

    NET_LOG("read: success, bytes_read=%zu\n", bytes_read);
    memcpy(buffer, response_buffer, bytes_read);
    kfree(response_buffer);

    return bytes_read;
}

// Запись данных в файл
ssize_t net_backend_write(struct rafs_file_info *file, const char *buffer, size_t len, loff_t offset) {
    char *encoded_data;
    int64_t result;

    NET_LOG("write: ino=%lu, len=%zu, offset=%llu\n", file->ino, len, offset);

    if (file == NULL || buffer == NULL) {
        return -EINVAL;
    }

    // Выделяем память для URL-encoded данных (каждый байт может стать до 3 байт)
    encoded_data = kmalloc(len * 3 + 1, GFP_KERNEL);
    if (encoded_data == NULL) {
        return -ENOMEM;
    }

    // URL-encode данные
    encode(buffer, encoded_data);

    NET_LOG("write: encoded_data length=%zu\n", strlen(encoded_data));

    char id_str[32], offset_str[32], buf_param[256+10];
    char *params[3];

    snprintf(id_str, sizeof(id_str), "id=%lu", file->ino);
    snprintf(offset_str, sizeof(offset_str), "offset=%llu", offset);
    // Используем buf=... для передачи encoded данных
    snprintf(buf_param, sizeof(buf_param), "buf=%s", encoded_data);

    params[0] = id_str;
    params[1] = offset_str;
    params[2] = buf_param;

    NET_LOG("write: params[0]='%s', params[1]='%s', params[2]='%s'\n", params[0], params[1], params[2]);

    result = rafs_http_call(TOKEN, "write", NULL, 0, 3,
                           params[0], params[1], params[2]);

    kfree(encoded_data);

    if (result < 0) {
        NET_LOG("write: http_call failed with %lld\n", result);
        return result;
    }

    NET_LOG("write: success, bytes_written=%zu\n", len);
    return len;  // result содержит количество записанных байт
}

// Чтение содержимого директории
int net_backend_readdir(struct super_block *sb, ino_t dir_ino, struct dir_context *ctx) {
    struct readdir_entry entry;
    int64_t result;
    loff_t offset = ctx->pos;

    NET_LOG("readdir: dir_ino=%lu, ctx->pos=%llu\n", dir_ino, ctx->pos);

    while (1) {
        char id_str[32], offset_str[32];
        snprintf(id_str, sizeof(id_str), "id=%lu", dir_ino);
        snprintf(offset_str, sizeof(offset_str), "offset=%llu", offset);

        result = rafs_http_call(TOKEN, "iterate", (char*)&entry, sizeof(entry), 2,
                               id_str, offset_str);

        NET_LOG("readdir: iterate result=%lld, offset=%llu\n", result, offset);

        if (result < 0) {
            // Нет больше записей или ошибка
            NET_LOG("readdir: no more entries (result=%lld)\n", result);
            break;
        }

        // Очищаем имя от null-байтов
        entry.name[255] = '\0'; // Гарантируем завершение строки
        char *name_end = memchr(entry.name, '\0', 256);
        if (name_end) {
            *name_end = '\0';
        }

        NET_LOG("readdir: entry ino=%u, name='%s', mode=%u\n", entry.ino, entry.name, entry.mode);

        // Добавляем запись в контекст
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