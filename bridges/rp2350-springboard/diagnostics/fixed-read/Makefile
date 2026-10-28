.DELETE_ON_ERROR:
CC = m68k-palmos-gcc
all: build/cs1-fixed-e2.prc
build/fixed: fixed.c access.S
	mkdir -p build
	$(CC) -O2 -Wall -Werror -palmos3.5 -o $@ fixed.c access.S
	m68k-palmos-objdump -d $@ > build/disassembly.txt
build/resources: fixed.rcp
	mkdir -p build
	cp fixed.rcp build/
	cd build && pilrc -q fixed.rcp
	touch $@
build/cs1-fixed-e2.prc: build/fixed build/resources fixed.def
	build-prc -o $@ fixed.def build/fixed build/*.bin
	python3 -c 'from pathlib import Path; p=Path("$@"); b=bytearray(p.read_bytes()); b[36:48]=bytes(12); p.write_bytes(b)'
