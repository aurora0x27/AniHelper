source_files := $(wildcard ./*.c)

debug_op := -g -O0 -fsanitize=address

release_op := -O3 -static

debug : $(source_files)
	gcc $(debug_op) $(source_files) -o ani-helper-debug

release : $(source_files)
	gcc $(release_op) $(source_files) -o ani-helper

all : debug release

clean :
	rm -f ./ani-helper*

.PHONY: debug release clean
