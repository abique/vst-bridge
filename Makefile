all:
	make -C maker
	make -C plugin
	make -C host

install:
	make -C maker install
	make -C plugin install
	make -C host install

clean:
	make -C maker clean
	make -C plugin clean
	make -C host clean
