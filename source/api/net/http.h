#ifndef rafs_HTTP_H
#define rafs_HTTP_H

#include <linux/inet.h>

int64_t rafs_http_call(const char *token, const char *method,
                            char *response_buffer, size_t buffer_size,
                            size_t arg_size, ...);

void encode(const char *, char *);

#endif // rafs_HTTP_H
