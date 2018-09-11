obj-m += KernelModule-1.o
all:
	make -C /usr/src/linux-headers-3.13.0-141-generic/ M=$(PWD) modules

clean:
	make -C /usr/src/linux-headers-3.13.0-141-generic/ M=$(PWD) cleanp