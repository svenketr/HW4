#pragma once
#include "lsp.h"
#include "lsp_rpc.h"

typedef struct {
    unsigned int            port;
    unsigned int            nextConnID;
    bool                    running;
    Connection              *connection;
    std::map<unsigned int,Connection*> clients;
    std::set<std::string>   connections;
    std::queue<message*> inbox;
    pthread_mutex_t         mutex;
    pthread_t               readThread;
    pthread_t               writeThread;
    pthread_t               epochThread;

    pthread_t               rpcThread;
} lsp_server;

// API Methods
lsp_server* lsp_server_create(int port);
int  lsp_server_read(lsp_server* a_srv, void* pld, uint32_t* conn_id);
bool lsp_server_write(lsp_server* a_srv, void* pld, int lth, uint32_t conn_id);
bool lsp_server_close(lsp_server* a_srv, uint32_t conn_id);

// Internal Methods
void* ServerRpcThread(void *params);
void* ServerEpochThread(void *params);
void* ServerReadThread(void *params);
void* ServerWriteThread(void *params);
void cleanup_connection(Connection *s);


int rpc_init(Connection* conn, int connId );
message* rpc_read(CLIENT *clnt, int connid);
int rpc_write(Connection* conn, message& outmsg);
int rpc_destroy(CLIENT *clnt);

int rpc_receive(message *msg);
