obj-m+=hidefile.o
 
all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
	$(CC) user_space.c -o hidden

load:
	insmod hidefile.ko

unload:
	rmmod hidefile

hide:
	./hidden

clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
	rm hidden