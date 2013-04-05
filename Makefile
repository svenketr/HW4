CC = gcc
TARGET = server request worker
DEBUG = -g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = $(DEBUG)
LIBS = -lprotobuf -lpthread -lssl -lcrypto
RPCGENFLAGS = -C

all: pre-build $(TARGET)

pre-build: rpcfiles
	protoc --cpp_out=. lspmessage.proto
    
request: lsp_client.o lsp.o lspmessage.pb.o network.o lsp_rpc_xdr.o lsp_rpc_clnt.o
	$(CC) $(LFLAGS) -o $@ $@.cpp $^ $(LIBS)

worker: lsp_client.o lsp.o lspmessage.pb.o network.o lsp_rpc_xdr.o lsp_rpc_clnt.o
	$(CC) $(LFLAGS) -o $@ $@.cpp $^ $(LIBS)

server: lsp_server.o lsp.o lspmessage.pb.o network.o lsp_rpc_xdr.o lsp_rpc_svc.o
	$(CC) $(LFLAGS) -o $@ $@.cpp $^ $(LIBS)

lspmessage.pb.o: lspmessage.pb.cc
	$(CC) $(CFLAGS) $<

lsp_rpc_svc.o: lsp_rpc_svc.c 
	$(CC) $(CFLAGS) $<
	
lsp_rpc_clnt.o: lsp_rpc_clnt.c
	$(CC) $(CFLAGS) $<

lsp_rpc_xdr.o: lsp_rpc_xdr.c
	$(CC) $(CFLAGS) $<
    
%.o: %.c
	$(CC) $(CFLAGS) $<
    
%.o: %.cpp
	$(CC) $(CFLAGS) $<

rpcfiles: lsp_rpc.h

lsp_rpc.h: lsp_rpc.x
	rpcgen $(RPCGENFLAGS) -m lsp_rpc.x -o lsp_rpc_svc.c
	rpcgen $(RPCGENFLAGS) -l lsp_rpc.x -o lsp_rpc_clnt.c
	rpcgen $(RPCGENFLAGS) -h lsp_rpc.x -o lsp_rpc.h
	rpcgen $(RPCGENFLAGS) -c lsp_rpc.x -o lsp_rpc_xdr.c

clean:
	rm -f *.o 
	rm -f lspmessage.pb.h lspmessage.pb.cc
	rm -f $(TARGET)
	rm -f *~
	rm -f lsp_rpc_svc.* lsp_rpc_clnt.* lsp_rpc.h lsp_rpc_xdr.*
	rm -f Makefile.lsp_rpc
