obj-m += adc.o
crcc= /usr/local/xtools/arm-unknown-linux-uclibcgnueabi/bin/arm-unknown-linux-uclibcgnueabi-
cc= /usr/local/xtools/arm-unknown-linux-uclibcgnueabi/bin/arm-unknown-linux-uclibcgnueabi-gcc
#ccflags-y := -std=c99 -Wno-declaration-after-statement
all:
	make ARCH=arm CROSS_COMPILE=$(crcc) -C /home/student/felabs/sysdev/tinysystem/linux-2.6.34 M=$(PWD) modules

clean:
	make ARCH=arm CROSS_COMPILE=$(crcc) -C /home/student/felabs/sysdev/tinysystem/linux-2.6.34 M=$(PWD) clean