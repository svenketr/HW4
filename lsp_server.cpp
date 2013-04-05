#include "lsp_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>

double epoch_delay = _EPOCH_LTH; // number of seconds between epochs
unsigned int num_epochs = _EPOCH_CNT; // number of epochs that are allowed to pass before a connection is terminated

/*
 *				LSP RELATED FUNCTIONS
 */  

// Set length of epoch (in seconds)
void lsp_set_epoch_lth(double lth){
	if(lth > 0)
		epoch_delay = lth;
}

// Set number of epochs before timing out
void lsp_set_epoch_cnt(int cnt){
	if(cnt > 0)
		num_epochs = cnt;
}

// Set fraction of packets that get dropped along each connection
void lsp_set_drop_rate(double rate){
	network_set_drop_rate(rate);
}

/*
 *
 *
 *				SERVER RELATED FUNCTIONS
 *
 *
 */  
void convert_lspmsg2msg(LSPMessage* lspmsg, message* msg)
{
	msg->connid = lspmsg->connid();
	msg->seqnum = lspmsg->seqnum();
	strcpy(msg->payload, lspmsg->payload().c_str());
}

void convert_msg2lspmsg(message* msg,LSPMessage* lspmsg)
{
	lspmsg->set_connid(msg->connid );
	lspmsg->set_seqnum(msg->seqnum);
	lspmsg->set_payload(msg->payload);
}

// A global variable to hold the server information
lsp_server *server_ptr;

// Create a server listening on a specified port.
// Returns NULL if server cannot be created
lsp_server* lsp_server_create(int port){
	// initialize the server data structure
	lsp_server *server = new lsp_server();
	server->port = port;
	server->nextConnID = 1;
	server->running = true;
	server->connection = new Connection(); // network_setup_server(port);

	if(!server->connection){
		// the server could not be bound to the specified port
		delete server;
		return NULL;
	}

	// initialize the mutex
	pthread_mutex_init(&(server->mutex),NULL);

	// create the epoch thread
	int res;
	if((res = pthread_create(&(server->rpcThread), NULL, ServerRpcThread, (void*)server)) != 0){
		printf("Error: Failed to start the rpc thread: %d\n",res);
		lsp_server_close(server,0);
		return NULL;
	}


	if((res = pthread_create(&(server->epochThread), NULL, ServerEpochThread, (void*)server)) != 0){
		printf("Error: Failed to start the epoch thread: %d\n",res);
		lsp_server_close(server,0);
		return NULL;
	}

//	// create the read/write threads listening on a certain port
//	if((res = pthread_create(&(server->readThread), NULL, ServerReadThread, (void*)server)) != 0){
//		printf("Error: Failed to start the epoch thread: %d\n",res);
//		lsp_server_close(server,0);
//		return NULL;
//	}
	if((res = pthread_create(&(server->writeThread), NULL, ServerWriteThread, (void*)server)) != 0){
		printf("Error: Failed to start the write thread: %d\n",res);
		lsp_server_close(server,0);
		return NULL;
	}
	server_ptr = server;

	return server;
}

// Read from connection. Return NULL when connection lost
// Returns number of bytes read. conn_id is an output parameter
int lsp_server_read(lsp_server* a_srv, void* pld, uint32_t* conn_id){
	// block until a message arrives or the client becomes disconnected
	while(true){
		pthread_mutex_lock(&(a_srv->mutex));
		bool running = a_srv->running;
		LSPMessage *msg = NULL;
		if(running) {
			// try to pop a message from the inbox queue
			if(a_srv->inbox.size() > 0){
				msg = a_srv->inbox.front();
				a_srv->inbox.pop();
			}
		}
		pthread_mutex_unlock(&(a_srv->mutex));
		if(!running)
			break;
		if(msg){
			// we got a message, return it
			std::string payload = msg->payload();
			*conn_id = msg->connid();
			delete msg;
			memcpy(pld,payload.c_str(),payload.length()+1);
			return payload.length();
		}

		// still connected, but no message has arrived...
		// sleep for a bit
		usleep(10000); // 10 ms = 10,0000 microseconds
	}
	*conn_id = 0; // all clients are disconnected
	return 0; // NULL, no bytes read (all clients disconnected)
}

// Server Write. Should not send NULL
bool lsp_server_write(lsp_server* a_srv, void* pld, int lth, uint32_t conn_id){
	if(pld == NULL || lth == 0)
		return false; // don't send bad messages

	pthread_mutex_lock(&(a_srv->mutex));
	Connection *conn = a_srv->clients[conn_id];
	conn->lastSentSeq++;
	if(DEBUG) printf("Server queueing msg %d for conn %d for write\n",conn->lastSentSeq,conn->id);

	// build the message object

	message *_msg = rpc_build_message(conn->id,conn->lastSentSeq,(uint8_t*)pld,lth);

	LSPMessage *msg = new LSPMessage();
	convert_msg2lspmsg(_msg, msg);
	delete _msg;

	// queue it up for writing
	conn->outbox.push(msg);
	pthread_mutex_unlock(&(a_srv->mutex));

	return true;
}

// Close connection.
bool lsp_server_close(lsp_server* a_srv, uint32_t conn_id){
	if(conn_id == 0){
		// close all connections
		if(DEBUG) printf("Shutting down the server\n");

		for(std::map<unsigned int,Connection*>::iterator it = a_srv->clients.begin();
				it != a_srv->clients.end();
				++it){
			Connection *conn = it->second;
			cleanup_connection(conn);
		}
		a_srv->clients.clear();
		a_srv->connections.clear();
		a_srv->running = false;

		// wait for the threads to terminate
		void *status;
		if(a_srv->readThread)
			pthread_join(a_srv->readThread,&status);
		if(a_srv->writeThread)
			pthread_join(a_srv->writeThread,&status);
		if(a_srv->epochThread)
			pthread_join(a_srv->epochThread,&status);

		pthread_mutex_destroy(&(a_srv->mutex));
		cleanup_connection(a_srv->connection);
		delete a_srv;
	} else {
		// close one connection
		if(DEBUG) printf("Shutting down client %d\n",conn_id);
		Connection *conn = a_srv->clients[conn_id];
		delete conn->addr;
		delete conn;
		a_srv->clients.erase(conn_id);
	}
	return false;
}

/* Internal Methods */

void* ServerEpochThread(void *params){
	lsp_server *server = (lsp_server*)params;

	while(true){
		usleep(epoch_delay * 1000000); // convert seconds to microseconds
		if(DEBUG) printf("Server epoch handler waking up\n");

		// epoch is happening; send required messages
		pthread_mutex_lock(&(server->mutex));
		if(!server->running)
			break;

		// iterate over all of the connections and apply the Epoch rules for each one
		for(std::map<unsigned int,Connection*>::iterator it=server->clients.begin();
				it != server->clients.end();
				++it){

			Connection *conn = it->second;

			if(conn->status == DISCONNECTED)
				continue;

			// send ACK for most recent message
			if(DEBUG) printf("Server acknowledging last received message %d for conn %d\n",conn->lastReceivedSeq,conn->id);
			rpc_acknowledge(conn);

			// resend the first message in the outbox, if any
			if(conn->outbox.size() > 0) {
				if(DEBUG) printf("Server resending msg %d for conn %d\n",conn->outbox.front()->seqnum(),conn->id);
				rpc_send_message(conn->clnt, conn->outbox.front());
			}

			if(++(conn->epochsSinceLastMessage) >= num_epochs){
				// oops, we haven't heard from the client in a while;
				// mark the connection as disconnected
				if(DEBUG) printf("Too many epochs have passed since we heard from client %d... disconnecting\n",conn->id);
				conn->status = DISCONNECTED;

				// place a "disconnected" message in the queue to notify the client
				message *_msg = rpc_build_message(conn->id,0,NULL,0);
			    LSPMessage *msg = new LSPMessage();
			    convert_msg2lspmsg(_msg, msg);
			    delete _msg;

				server->inbox.push(msg);
			}
		}

		pthread_mutex_unlock(&(server->mutex));
	}
	pthread_mutex_unlock(&(server->mutex));
	if(DEBUG) printf("Epoch Thread exiting\n");
	return NULL;
}

// this write thread will ensure that messages can be sent/received faster than only
// on epoch boundaries. It will continuously poll for messages that are eligible to
// bet sent for the first time, and then send them out.
void* ServerWriteThread(void *params){
	lsp_server *server = (lsp_server*)params;

	// continuously poll for new messages to send

	// store the last sent seq number for each client so that
	// we only send each message once
	std::map<unsigned int, unsigned int> lastSent;

	while(true){
		pthread_mutex_lock(&(server->mutex));
		if(!server->running)
			break;

		// iterate through all clients and see if they have messages to send
		for(std::map<unsigned int,Connection*>::iterator it=server->clients.begin();
				it != server->clients.end();
				++it){

			Connection *conn = it->second;

			if(conn->status == DISCONNECTED)
				continue;

			unsigned int nextToSend = conn->lastReceivedAck + 1;
			if(nextToSend > lastSent[conn->id]){
				// we have received an ack for the last message, and we haven't sent the
				// next one out yet, so if it exists, let's send it now
				if(conn->outbox.size() > 0) {
					rpc_send_message(conn->clnt, conn->outbox.front());
//					network_send_message(conn,conn->outbox.front());
					lastSent[conn->id] = conn->outbox.front()->seqnum();
				}
			}
		}
		pthread_mutex_unlock(&(server->mutex));
		usleep(5000); // 5ms
	}
	pthread_mutex_unlock(&(server->mutex));
	return NULL;
}

/* Return values
 * -1 --> msg was empty
 *  0 --> server not running
 * >0 --> actual connection id for client
 *
 */
int rpc_receive(message *msg)
{
	char host[128];

	pthread_mutex_lock(&(server_ptr->mutex));
	if(!server_ptr->running)
		return 0;
	pthread_mutex_unlock(&(server_ptr->mutex));
	int connId = -1;
	sockaddr_in addr;
	if(msg) {
		// we got a message, let's parse it
		pthread_mutex_lock(&(server_ptr->mutex));
		if(msg->connid == 0 && msg->seqnum == 0 && strlen(msg->payload) == 0){
			// connection request, if first time, make the connection
			sprintf(host,"%s:%d",inet_ntoa(addr.sin_addr),addr.sin_port);
			if(server_ptr->connections.count(host) == 0){
				// this is the first time we've seen this host, add it to the server's list of seen hosts
				server_ptr->connections.insert(host);

				if(DEBUG) printf("Connection request received from %s\n",host);

				// build up the new connection object
				Connection *conn = new Connection();
				conn->status = CONNECTED;
				conn->id = server_ptr->nextConnID;
				server_ptr->nextConnID++;
				conn->lastSentSeq = 0;
				conn->lastReceivedSeq = 0;
				conn->epochsSinceLastMessage = 0;
				conn->fd = server_ptr->connection->fd; // send through the server's socket
				conn->addr = new sockaddr_in();
				rpc_init(conn->clnt,conn->id);
				memcpy(conn->addr,&addr,sizeof(addr));

				LSPMessage *_msg = new LSPMessage();
				convert_msg2lspmsg(msg, _msg);

				// send an ack for the connection request
				conn->outbox.push(_msg);

				// insert this connection into the list of connections
				server_ptr->clients.insert(std::pair<int,Connection*>(conn->id,conn));
				connId = conn->id;
			}
		} else {
			if(server_ptr->clients.count(msg->connid) == 0){
				printf("Bogus connection id received: %d, skipping message...\n",msg->connid);
			} else {
				Connection *conn = server_ptr->clients[msg->connid];
				connId = msg->connid;

				// reset counter for epochs since we have received a message
				conn->epochsSinceLastMessage = 0;

				if(strlen(msg->payload) == 0){
					// we received an ACK
					if(DEBUG) printf("Server received an ACK for conn %d msg %d\n",msg->connid,msg->seqnum);
					if(msg->seqnum == (conn->lastReceivedAck + 1))
						conn->lastReceivedAck = msg->seqnum;
					if(conn->outbox.size() > 0 && msg->seqnum == conn->outbox.front()->seqnum()) {
						delete conn->outbox.front();
						conn->outbox.pop();
					}
				} else {
					// data packet
					if(DEBUG) printf("Server received msg %d for conn %d\n",msg->seqnum,msg->connid);
					if(msg->seqnum == (conn->lastReceivedSeq + 1)){
						// next in the list
						conn->lastReceivedSeq++;
						//XXX server_ptr->inbox.push(msg);

						LSPMessage m;
						convert_msg2lspmsg(rpc_acknowledge(conn), &m);
						// send an ack for the connection request
						conn->outbox.push(&m);
					}
				}
			}
		}
		pthread_mutex_unlock(&(server_ptr->mutex));
	}
	return connId;
}

int rpc_init(CLIENT* &clnt, int connId )
{
	clnt = clnt_create("localhost", LSP_PROG+connId, LSP_VERS, "udp");
	if (clnt == NULL) {
		clnt_pcreateerror("localhost");
		exit(1);
	}
	return 0;
}

message* rpc_read(CLIENT *clnt, int connid)
{
//    while(true){
//    	message* inmsg = send_1(&connid, clnt);
//    	if(inmsg != NULL)
//    	{
//    		/* process the message */
//    		return inmsg;
//    	}
//    	usleep(10000);
//    }
}

message* rpc_acknowledge(Connection *conn){
    message *msg = rpc_build_message(conn->id,conn->lastReceivedSeq,NULL,0);
    rpc_write(conn->clnt, *msg);
    return msg;
}

bool rpc_send_message(CLIENT *clnt, LSPMessage *lspmsg)
{
    // sends an LSP Message
    if(DEBUG) printf("RPC:: Sending message (%d,%d,\"%s\")\n",
    		lspmsg->connid(),lspmsg->seqnum(),lspmsg->payload().c_str());

    message msg;
    convert_lspmsg2msg(lspmsg, &msg);
	rpc_write(clnt, msg);

    return true;
}


int rpc_write(CLIENT *clnt, message& outmsg)
{
	int* ret_val;
	ret_val = receive_1(&outmsg, clnt);	/* call the remote function */

	/* test if the RPC succeeded */
	if (ret_val == NULL) {
		clnt_perror(clnt, "call failed:");
		exit(1);
	}

	printf("rpc_write done: %d\n", *ret_val);
	return 0;
}

int rpc_destroy(CLIENT *clnt)
{
	clnt_destroy( clnt );
	return 0;
}

int* receive_1_svc(message *msg, struct svc_req *rqstp)
{
	static int  result ;

	if(DEBUG) printf("Received on server: conn: %d, seqnum: %d \n", msg->connid, msg->seqnum);
	result = rpc_receive(msg);

	return &result;
}


void* ServerRpcThread(void *params){
	lsp_server *server = (lsp_server*)params;
	register SVCXPRT *transp;

	pmap_unset (LSP_PROG, LSP_VERS);

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL) {
		fprintf (stderr, "%s", "cannot create udp service.");
		exit(1);
	}
	if (!svc_register(transp, LSP_PROG, LSP_VERS, lsp_prog_1, IPPROTO_UDP)) {
		fprintf (stderr, "%s", "unable to register (TEST_PROG, TEST_VERS, udp).");
		exit(1);
	}

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		fprintf (stderr, "%s", "cannot create tcp service.");
		exit(1);
	}
	if (!svc_register(transp, LSP_PROG, LSP_VERS, lsp_prog_1, IPPROTO_TCP)) {
		fprintf (stderr, "%s", "unable to register (TEST_PROG, TEST_VERS, tcp).");
		exit(1);
	}

	svc_run ();
	fprintf (stderr, "%s", "svc_run returned");
	return NULL;
}

void cleanup_connection(Connection *s){
	if(!s)
		return;

	// close the file descriptor and free memory
	if(s->fd != -1)
	{
		rpc_destroy(s->clnt);
		close(s->fd);
	}
	delete s->addr;
	delete s;
}

