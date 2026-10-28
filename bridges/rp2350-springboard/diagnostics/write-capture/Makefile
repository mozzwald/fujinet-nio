.DELETE_ON_ERROR:
CC=m68k-palmos-gcc
all: build/cs1-write-e3.prc
build/write: write.c access.S
	mkdir -p build
	$(CC) -O2 -Wall -Werror -palmos3.5 -o $@ write.c access.S
	m68k-palmos-objdump -d $@ > build/disassembly.txt
build/resources: write.rcp
	mkdir -p build
	cp write.rcp build/
	cd build && pilrc -q write.rcp
	touch $@
build/cs1-write-e3.prc: build/write build/resources write.def
	build-prc -o $@ write.def build/write build/*.bin
	python3 -c 'from pathlib import Path; p=Path("$@"); b=bytearray(p.read_bytes()); b[36:48]=bytes(12); p.write_bytes(b)'
