CC = gcc
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

server: lsp_server.o lspmessage.pb.o network.o lsp_rpc_svc.o lsp_rpc_xdr.o
	$(CC) $(LFLAGS) -o $@ $@.cpp $^ $(LIBS)

lspmessage.pb.o: lspmessage.pb.cc
	$(CC) $(CFLAGS) $<
    
%.o: %.c
	$(CC) $(CFLAGS) $<
    
%.o: %.cpp
	$(CC) $(CFLAGS) $<
	
lsp_rpc.h: lsp_rpc.x
	rpcgen -m lsp_rpc.x -o lsp_rpc_svc.c
	rpcgen -l lsp_rpc.x -o lsp_rpc_clnt.c
	rpcgen -h lsp_rpc.x -o lsp_rpc.h
	rpcgen -c lsp_rpc.x -o lsp_rpc_xdr.c

clean:
	rm -f *.o 
	rm -f lspmessage.pb.h lspmessage.pb.cc
	rm -f $(TARGET)
	rm -f *~
	rm -f lsp_rpc_svc.* lsp_rpc_clnt.* lsp_rpc.h lsp_rpc_xdr.*
	rm Makefile.lsp_rpc
