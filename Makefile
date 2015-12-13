all:
	cd src/usr.sbin/erps-ctl && make
	cd src/sys/modules/erps && make

install:
	rm -f /boot/kernel/erps.ko /boot/kernel/erps.ko.symbols
	cd src/sys/modules/erps && make install
	cd src/usr.sbin/erps-ctl && make install

clean:
	cd src/sys/modules/erps && make clean
	cd src/usr.sbin/erps-ctl && make clean

load:
	cd src/sys/modules/erps && make load

unload:
	cd src/sys/modules/erps && make unload || true

