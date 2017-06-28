
all: r_attenu r_attenuc

r_attenu: r_attenu.c
	gcc -o r_attenu r_attenu.c -I/usr/local/include/lirc/  -I/usr/local/include/lirc/include/ -L/usr/local/lib/ -lwiringPi -llirc -llirc_client

r_attenuc: r_attenuc.c
	gcc -o r_attenuc r_attenuc.c

clean:
	rm -rf r_attenu r_attenuc

