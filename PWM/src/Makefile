obj-m += mpwm.o
mpwm-objs += pwm.o file.o
crcc= /usr/local/xtools/arm-unknown-linux-uclibcgnueabi/bin/arm-unknown-linux-uclibcgnueabi-
cc= /usr/local/xtools/arm-unknown-linux-uclibcgnueabi/bin/arm-unknown-linux-uclibcgnueabi-gcc
all:
	make ARCH=arm CROSS_COMPILE=$(crcc) -C /home/student/felabs/sysdev/tinysystem/linux-2.6.34 M=$(PWD) modules

clean:
	make ARCH=arm CROSS_COMPILE=$(crcc) -C /home/student/felabs/sysdev/tinysystem/linux-2.6.34 M=$(PWD) clean