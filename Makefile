gestor: tarefa.o main.o commandlinereader.o
	gcc -o gestor tarefa.o main.o commandlinereader.o
tarefa.o: tarefa.h tarefa.c
	gcc -g -c tarefa.c

main.o: main.c tarefa.h
	gcc -g -c main.c

commandlinereader.o: commandlinereader.h commandlinereader.c
	gcc -g -c commandlinereader.c
