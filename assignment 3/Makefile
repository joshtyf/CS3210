build:
	mpicc -c tasks.c
	mpicc -c utils.c
	mpic++ -o a03 main.cpp tasks.o utils.o

build_dynamic:
	mpicc -c tasks.c
	mpicc -c utils.c
	mpic++ -o a03_dynamic main_dynamic.cpp tasks.o utils.o