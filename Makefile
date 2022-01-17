compile:
	gcc main.c cor.s csgo.c -g  -lpthread -std=c11
run:
	gcc -fPIC -fPIE main.c cor.s csgo.c -g -w -gstabs  -lpthread -std=c11
	./a.out
