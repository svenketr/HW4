// John Keech, UIN:819000713, CSCE 438 - HW2

// This file creates the Live Sequence Protocol (LSP)
// API that can be used by both clients and servers.

#ifndef LSP_API
#define LSP_API

#include "datatypes.h"
#include "network.h"

// Global Parameters. For both server and clients.
#define _EPOCH_LTH 2.0
#define _EPOCH_CNT 5

typedef unsigned long long timestamp_t;
timestamp_t get_timestamp ();

// Set length of epoch (in seconds)
void lsp_set_epoch_lth(double lth);

// Set number of epochs before timing out
void lsp_set_epoch_cnt(int cnt);

// Set fraction of packets that get dropped along each connection
void lsp_set_drop_rate(double rate);

message* rpc_build_message(const message* msg);
message* rpc_build_message(int id, int seq, uint8_t *pld, int len);

extern "C" void lsp_prog_1(struct svc_req *rqstp, register SVCXPRT *transp);

int lsp_prog_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result);

// acknowledge the last received message
message* rpc_acknowledge(Connection *conn);

message* rpc_build_message(int id, int seq, uint8_t *pld, int len);

#endif
