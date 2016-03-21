CC=gcc
CCOPTS=-ffreestanding
INSTALL=/usr/sbin/install

default: random

random.o: random.c
	$(CC) $(CCOPTS) -D_KERNEL -c random.c

random: random.o
	ld -r -o random random.o

clean:
	rm -f *.o random

install: random random.conf
	$(INSTALL) -f /kernel/drv -m 0755 -u root -g sys random
	$(INSTALL) -f /kernel/drv -m 0644 -u root -g sys random.conf
	/usr/sbin/add_drv -m '* 0444 root sys' random
	ln -s /devices/pseudo/random@0:random /dev/random
	ln -s /devices/pseudo/random@0:urandom /dev/urandom

uninstall:
	/usr/sbin/rem_drv random
	rm -f /kernel/drv/random
	rm -f /kernel/drv/random.conf
	rm -f /dev/random
	rm -f /dev/urandom

.PHONY: install uninstall
