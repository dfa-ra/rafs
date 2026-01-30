#include "http.h"

const char *SERVER_IP = "10.0.2.2";
const int SERVER_PORT = 8081;

// callee should call free_request on received buffer
int fill_request(struct kvec *vec, const char *token, const char *method,
                 size_t arg_size, va_list args) {
  // 2048 bytes for URL and 64 bytes for anything else
  const int BUFFER_SIZE = 2048 + 64;
  char *request_buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);
  if (request_buffer == 0) {
    pr_info("[rafs-http] fill_request: kzalloc failed\n");
    return -ENOMEM;
  }
  strcpy(request_buffer, "GET /api/");
  strcat(request_buffer, method);

  strcat(request_buffer, "?token=");
  strcat(request_buffer, token);

  for (int i = 0; i < arg_size; i++) {
    if (strlen(request_buffer) + 1 >= BUFFER_SIZE) {
      kfree(request_buffer);
      return -ENOSPC;
    }
    strcat(request_buffer, "&");
    char *param = va_arg(args, char *);
    if (param == NULL) {
      kfree(request_buffer);
      return -EINVAL;
    }
    if (strlen(request_buffer) + strlen(param) >= BUFFER_SIZE) {
      kfree(request_buffer);
      return -ENOSPC;
    }
    strcat(request_buffer, param);
  }

  strcat(request_buffer, " HTTP/1.1\r\nHost:");
  strcat(request_buffer, SERVER_IP);
  strcat(request_buffer, "\r\nConnection: close\r\n\r\n");

  memset(vec, 0, sizeof(struct kvec));
  vec->iov_base = request_buffer;
  vec->iov_len = strlen(request_buffer);

  return 0;
}

int receive_all(struct socket *sock, char *buffer, size_t buffer_size) {
  struct msghdr hdr;
  struct kvec vec;

  int read = 0;

  while (read < buffer_size) {
    memset(&hdr, 0, sizeof(struct msghdr));
    memset(&vec, 0, sizeof(struct kvec));
    vec.iov_base = buffer + read;
    vec.iov_len = buffer_size - read;
    int ret = kernel_recvmsg(sock, &hdr, &vec, 1, vec.iov_len, 0);
    if (ret == 0) {
      break;
    } else if (ret < 0) {
      return -4;
    }
    read += ret;
  }

  return read;
}

int64_t parse_http_response(char *raw_response, size_t raw_response_size,
                            char *response, size_t response_size) {
  char *buffer = raw_response;

  // Read Response Line
  {
    char *status_line = strsep(&buffer, "\r");
    strsep(&status_line, " ");
    if (status_line == 0) {
      return -6;
    }
    char *status_code = strsep(&status_line, " ");
    if (strcmp(status_code, "200") != 0) {
      return -5;
    }
  }

  int length = -1;

  while (true) {
    if (buffer == 0) {
      return -6;
    }
    char *header = strsep(&buffer, "\r");
    ++header; // skip \n
    if (strcmp(header, "") == 0) {
      // end of headers
      break;
    }

    if (strncasecmp(header, "content-length:", 15) == 0) {
      char *value = header + 15;
      while (*value == ' ' || *value == '\t') value++;  // skip spaces
      int error = kstrtoint(value, 0, &length);
      if (error != 0) {
        return -6;
      }
    }
  }
  ++buffer; // skip last '\n'

  if (length == -1) {
    return -6;
  }

  if (buffer + length > raw_response + raw_response_size) {
    return -6;
  }

  if (length < sizeof(int64_t)) {
    return -7;
  }

  length -= sizeof(int64_t);

  if (length > response_size) {
    return -ENOSPC;
  }

  int64_t return_value;
  memcpy(&return_value, buffer, sizeof(int64_t));

  buffer += sizeof(int64_t);
  memcpy(response, buffer, length);

  return return_value;
}

int64_t rafs_http_call(const char *token, const char *method,
                            char *response_buffer, size_t buffer_size,
                            size_t arg_size, ...) {
  struct socket *sock;
  int64_t error;

  pr_info("[rafs-http] http_call: token='%s', method='%s', buffer_size=%zu, arg_size=%zu\n",
          token, method, buffer_size, arg_size);

  pr_info("[rafs-http] creating socket\n");
  error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
  if (error < 0) {
    pr_info("[rafs-http] sock_create_kern failed: %lld\n", error);
    return -1;
  }
  if (sock == NULL) {
    pr_info("[rafs-http] sock is NULL after creation\n");
    return -1;
  }

  struct sockaddr_in s_addr = {.sin_family = AF_INET,
                               .sin_addr = {.s_addr = in_aton(SERVER_IP)},
                               .sin_port = htons(SERVER_PORT)};

  pr_info("[rafs-http] connecting to %s:%d\n", SERVER_IP, SERVER_PORT);
  error = kernel_connect(sock, (struct sockaddr *)&s_addr,
                         sizeof(struct sockaddr_in), 0);
  if (error != 0) {
    pr_info("[rafs-http] kernel_connect failed: %lld\n", error);
    sock_release(sock);
    return -2;
  }
  pr_info("[rafs-http] connected successfully\n");

  struct kvec kvec;
  va_list args;
  va_start(args, arg_size);
  pr_info("[rafs-http] calling fill_request\n");
  error = fill_request(&kvec, token, method, arg_size, args);
  va_end(args);
  pr_info("[rafs-http] fill_request returned %lld\n", error);

  if (error != 0) {
    kernel_sock_shutdown(sock, SHUT_RDWR);
    sock_release(sock);
    return error;
  }

  struct msghdr msg;
  memset(&msg, 0, sizeof(struct msghdr));

  pr_info("[rafs-http] calling kernel_sendmsg, iov_len=%zu\n", kvec.iov_len);
  error = kernel_sendmsg(sock, &msg, &kvec, 1, kvec.iov_len);
  pr_info("[rafs-http] kernel_sendmsg returned %lld\n", error);
  kfree(kvec.iov_base);

  if (error < 0) {
    pr_info("[rafs-http] kernel_sendmsg failed: %lld\n", error);
    kernel_sock_shutdown(sock, SHUT_RDWR);
    sock_release(sock);
    return -3;
  }

  size_t raw_buffer_size = buffer_size + 1024; // add 1KB for HTTP headers
  pr_info("[rafs-http] allocating raw_response_buffer, size=%zu\n", raw_buffer_size);
  char *raw_response_buffer = kmalloc(raw_buffer_size, GFP_KERNEL);
  if (raw_response_buffer == 0) {
    pr_info("[rafs-http] kmalloc failed for raw_response_buffer\n");
    kernel_sock_shutdown(sock, SHUT_RDWR);
    sock_release(sock);
    return -ENOMEM;
  }
  pr_info("[rafs-http] calling receive_all\n");
  int read_bytes = receive_all(sock, raw_response_buffer, raw_buffer_size);
  pr_info("[rafs-http] receive_all returned %d\n", read_bytes);

  kernel_sock_shutdown(sock, SHUT_RDWR);
  sock_release(sock);

  if (read_bytes < 0) {
    kfree(raw_response_buffer);
    return -4;
  }

  pr_info("[rafs-http] received %d bytes total\n", read_bytes);
  error = parse_http_response(raw_response_buffer, read_bytes, response_buffer,
                              buffer_size);

  kfree(raw_response_buffer);
  return error;
}

void encode(const char *src, char *dst) {
  while (*src != '\0') {
    if ((*src >= '0' && *src <= '9') || (*src >= 'a' && *src <= 'z') ||
        (*src >= 'A' && *src <= 'Z')) {
      *dst = *src;
      dst++;
    } else {
      sprintf(dst, "%%%02X", (unsigned char)*src);
      dst += 3;
    }
    src++;
  }
  *dst = '\0';
}
