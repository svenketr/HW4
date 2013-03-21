CC = g++
TARGET = server request worker
DEBUG = #-g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = $(DEBUG)
LIBS = -lprotobuf -lpthread -lssl -lcrypto

all: pre-build $(TARGET)

pre-build:
	protoc --cpp_out=. lspmessage.proto
    
request: lsp_client.o lspmessage.pb.o network.o
	$(CC) $(LFLAGS) -o $@ $@.cpp $^ $(LIBS)

worker: lsp_client.o lspmessage.pb.o network.o
	$(CC) $(LFLAGS) -o $@ $@.cpp $^ $(LIBS)

server: lsp_server.o lspmessage.pb.o network.o
	$(CC) $(LFLAGS) -o $@ $@.cpp $^ $(LIBS)

lspmessage.pb.o: lspmessage.pb.cc
	$(CC) $(CFLAGS) $<
    
%.o: %.cpp
	$(CC) $(CFLAGS) $<

clean:
	rm -f *.o 
	rm -f lspmessage.pb.h lspmessage.pb.cc
	rm -f $(TARGET)
	rm -f *~
