// John Keech, UIN:819000713, CSCE 438 - HW2

#ifndef NETWORK_HANDLER
#define NETWORK_HANDLER

#include "datatypes.h"

#define _DROP_RATE 0.0

// build a socket and try to connect
Connection* network_setup_server(int port);
Connection* network_make_connection(const char *host, int port);
bool network_send_connection_request(Connection *conn);
bool network_wait_for_connection(Connection *conn, double timeout); // wait for a connection for timeout seconds

// send and receive messages
bool network_send_message(Connection *conn, LSPMessage *msg);
LSPMessage* network_read_message(Connection *conn, double timeout, sockaddr_in *addr); // try to read a message with a timeout in seconds
bool network_acknowledge(Connection *conn); // send an acknowledgement from the client for the previously received message

// Marshal/unmarshal data using Google Protocol Buffers
LSPMessage* network_build_message(int is, int seq, uint8_t *pld, int len);
uint8_t* network_marshal(LSPMessage *msg, int *packedSize);
LSPMessage* network_unmarshal(uint8_t *buf, int buf_len);

// configure the network drop rate
void network_set_drop_rate(double percent);

// use the drop rate to determine if we should send/receive this packet
bool network_should_drop();

// create a timeval structure
struct timeval network_get_timeval(double seconds);

#endif
