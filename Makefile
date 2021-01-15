link: compile
	gcc -lncursesw bet.o -o bet 
	rm bet.o

compile:
	gcc -c -g bet.c -o bet.o

deneme :
	gcc d.c -o d -g -lncursesw

.PHONY: bet