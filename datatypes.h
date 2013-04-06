// John Keech, UIN:819000713, CSCE 438 - HW2

#ifndef DATATYPES_H
#define DATATYPES_H

#define DEBUG true

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <queue>
#include <map>
#include <set>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include <string>

#include "lsp_rpc.h"

typedef enum {
    DISCONNECTED, CONNECT_SENT, CONNECTED
} Status;

typedef struct {
    const char              *host;
    unsigned int            port;
    int                     fd;
    struct sockaddr_in      *addr;
    Status                  status;
    unsigned int            id;
    unsigned int            lastSentSeq;
    unsigned int            lastReceivedSeq;
    unsigned int            lastReceivedAck;
    unsigned int            epochsSinceLastMessage;
    std::queue<message*> outbox;

    CLIENT 					*clnt;
} Connection;

#endif
