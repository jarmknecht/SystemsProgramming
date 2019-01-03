CC = g++
CFLAGS = -g -Wall
SOURCES = generate.h generate.cpp random.h random.cpp graph.h graph.cpp dot.h dot.cpp
OBJECTS = generate.o random.o graph.o dot.o
EXECUTABLE = generate

%.o:%.cpp %.h
	$(CC) $(CFLAGS) -c $<

$(EXECUTABLE):$(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@


.phony:clean
clean:
	rm $(EXECUTABLE) $(OBJECTS)