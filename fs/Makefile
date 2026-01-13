obj-m += rafs.o
rafs-objs := source/rafs.o source/inode.o source/super.o source/dir.o source/file.o

PWD := $(CURDIR) 
KDIR = /lib/modules/`uname -r`/build
EXTRA_CFLAGS = -Wall -g

all:
	make -C $(KDIR) M=$(PWD) modules 

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -rf .cache
