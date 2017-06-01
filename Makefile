JZPFS_VERSION="0.1"
EXTRA_CFLAGS += -DJZPFS_VERSION=\"$(JZPFS_VERSION)\" $(EXTRA)

obj-m := jzpfs.o 
jzpfs-objs := dentry.o file.o inode.o main.o super.o lookup.o mmap.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm *.o *~
