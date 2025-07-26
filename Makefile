obj-m := pcd.o
ARCH=arm
HOST_KERN_DIR = /lib/modules/$(shell uname -r)/build

clean:
	make -C $(HOST_KERN_DIR) M=$(PWD) clean
host:
	make -C $(HOST_KERN_DIR) M=$(PWD) modules
