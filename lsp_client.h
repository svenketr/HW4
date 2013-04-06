#pragma once
#include "lsp.h"


typedef struct {
    Connection              *connection;
    std::queue<message*> inbox;
    pthread_mutex_t         mutex;
    pthread_t               readThread;
    pthread_t               writeThread;
    pthread_t               epochThread;
    pthread_t               rpcThread;
} lsp_client;

// API Methods
lsp_client* lsp_client_create(const char* dest, int port);
int lsp_client_read(lsp_client* a_client, uint8_t* pld);
bool lsp_client_write(lsp_client* a_client, uint8_t* pld, int lth);
bool lsp_client_close(lsp_client* a_client);

// Internal methods
void* ClientRpcThread(void *params);
void* ClientEpochThread(void *params);
void* ClientReadThread(void *params);
void* ClientWriteThread(void *params);
void cleanup_connection(Connection *s);
void cleanup_client(lsp_client *client);

int rpc_init(CLIENT*& clnt, const char* host);
bool rpc_send_conn_req(lsp_client* client);
message* rpc_read(CLIENT *clnt, int connid);
bool rpc_send_message(CLIENT *clnt, LSPMessage *lspmsg);
int rpc_write(CLIENT *clnt, message& outmsg);
int rpc_destroy(CLIENT *clnt);
int rpc_receive(message *msg);
