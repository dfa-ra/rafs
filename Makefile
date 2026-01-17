obj-m += rafs.o

rafs-objs := \
	source/rafs.o \
	source/super.o \
	source/inode.o \
	source/dir.o \
	source/file.o \
	source/api/select.o \
	source/api/ram/ram_backend.o \
	source/api/net/net_backend.o \
	source/api/net/http.o

PWD := $(CURDIR)
KDIR := /lib/modules/$(shell uname -r)/build

EXTRA_CFLAGS = -Wall -g -DRAFS_BACKEND_NET

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -rf .cache
