FLAGS=`pkg-config gtk+-3.0 --cflags`
LIBS=`pkg-config gtk+-3.0 --libs`

pmount-gui-ng: main.o
	gcc main.o -o pmount-gui-ng $(LIBS)

main.o: main.c
	gcc -g -c main.c -o main.o $(FLAGS)
clean:
	rm -f main.o
	rm -f pmount-gui-ng
