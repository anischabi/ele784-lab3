obj-m += buf_driver.o

BIN_DIR := ../../bin
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: $(BIN_DIR)
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	mv -f buf_driver.ko $(BIN_DIR)/

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(BIN_DIR)/buf_driver.ko