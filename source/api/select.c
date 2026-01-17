#include "api.h"

#ifdef RAFS_BACKEND_RAM
#include "ram/ram_backend.h"

#elif defined(RAFS_BACKEND_NET)
#include "net/net_backend.h"

#else
#error "Please define RAFS_BACKEND_RAM or RAFS_BACKEND_NET"
#endif


struct rafs_backend_ops rafs_backend_ops = {
#ifdef RAFS_BACKEND_RAM
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
#endif
#ifdef RAFS_BACKEND_NET
    .init = net_backend_init,
    .destroy = net_backend_destroy,
    .lookup = net_backend_lookup,
    .create = net_backend_create,
    .unlink = net_backend_unlink,
    .link = net_backend_link,
    .is_empty_dir = net_backend_is_empty_dir,
    .read = net_backend_read,
    .write = net_backend_write,
    .get_size = net_backend_get_size,
    .readdir = net_backend_readdir,
    .free_file_info = net_backend_free_file_info,
#endif
};
