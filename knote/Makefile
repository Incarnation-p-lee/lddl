ifneq ($(KERNELRELEASE),)
  obj-m     :=knote.o
#  obj-m     :=test.o
else
  KERNELDIR ?=/lib/modules/$(shell uname -r)/build
  PWD       :=$(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
clean:
	rm -rfv *.o *.mod.* *.ko *.order *.symvers
endif
# make -C /lib/modules/3.10.10/build M=`pwd` modules
#obj-m        :=usb_module_auto.o
#obj-m        :=test.o
