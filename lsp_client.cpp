#include "lsp_rpc.h"
#include "lsp_client.h"

#include <rpc/pmap_clnt.h>
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
 *				CLIENT RELATED FUNCTIONS
 */  

// A global variable to hold the client information
lsp_client *client_ptr;

lsp_client* lsp_client_create(const char* dest, int port){
	lsp_client *client = new lsp_client();
	pthread_mutex_init(&(client->mutex),NULL);
	client->connection = new Connection(); //network_make_connection(dest,port);

	client_ptr = client;
	if(!client->connection){
		// connection could not be made
		lsp_client_close(client);
		return NULL;
	}

	client->connection->lastSentSeq = 0;
	client->connection->lastReceivedSeq = 0;
	client->connection->lastReceivedAck = 0;
	client->connection->epochsSinceLastMessage = 0;

	// kickoff new epoch timer
	int res;

	if((res = pthread_create(&(client->epochThread), NULL, ClientEpochThread, (void*)client)) != 0){
		printf("Error: Failed to start the epoch thread: %d\n",res);
		lsp_client_close(client);
		return NULL;
	}

	rpc_init(client->connection->clnt, dest);

//	if(network_send_connection_request(client->connection) &&
//			network_wait_for_connection(client->connection, epoch_delay * num_epochs)){
	if(rpc_send_conn_req(client)) {
		pthread_mutex_lock(&(client->mutex));

		// connection succeeded, build lsp_client struct
		client->connection->port = port;
		client->connection->host = dest;
		client->connection->status = CONNECTED;

		// kick off ReadThread to catch incoming messages
		int res;
		if((res = pthread_create(&(client->rpcThread), NULL, ClientRpcThread, (void*)client)) != 0){
			printf("Error: Failed to start the rpc thread: %d\n",res);
			lsp_client_close(client);
			return NULL;
		}
		sleep(2);
		if((res = pthread_create(&(client->writeThread), NULL, ClientWriteThread, (void*)client)) != 0){
			printf("Error: Failed to start the write thread: %d\n",res);
			lsp_client_close(client);
			return NULL;
		}
		pthread_mutex_unlock(&(client->mutex));

		return client;
	} else {
		// connection failed or timeout after K * delta seconds
		lsp_client_close(client);
		return NULL;
	}
}

int lsp_client_read(lsp_client* a_client, uint8_t* pld){
	// block until a message arrives or the client becomes disconnected
	while(true){
		pthread_mutex_lock(&(a_client->mutex));
		Status s = a_client->connection->status;
		message *msg = NULL;
		if(s == CONNECTED) {
			// try to pop a message off of the inbox queue
			if(a_client->inbox.size() > 0){
				msg = a_client->inbox.front();
				a_client->inbox.pop();
			}
		}
		pthread_mutex_unlock(&(a_client->mutex));
		if(s == DISCONNECTED)
			break;

		// we got a message, so return it
		if(msg){
			std::string payload(msg->payload);
			delete msg;
			memcpy(pld,payload.c_str(),payload.length()+1);
			return payload.length();
		}

		// still connected, but no message has arrived...
		// sleep for a bit
		usleep(10000); // 10 ms = 10,0000 microseconds
	}
	if(DEBUG) printf("Client was disconnected. Read returning NULL\n");
	return 0; // NULL, no bytes read (client disconnected)
}

bool lsp_client_write(lsp_client* a_client, uint8_t* pld, int lth){
	// queues up a message to be written by the Write Thread

	if(pld == NULL || lth == 0)
		return false; // don't send bad messages

	pthread_mutex_lock(&(a_client->mutex));
	a_client->connection->lastSentSeq++;
	if(DEBUG) printf("Client queueing msg %d for write\n",a_client->connection->lastSentSeq);

	// build the message
	message *msg = rpc_build_message(a_client->connection->id,a_client->connection->lastSentSeq,pld,lth);

	// queue it up
	a_client->connection->outbox.push(msg);
	pthread_mutex_unlock(&(a_client->mutex));

	return true;
}

bool lsp_client_close(lsp_client* a_client){
	// returns true if the connected was closed,
	// false if it was already previously closed

	if(DEBUG) printf("Shutting down the client\n");

	pthread_mutex_lock(&(a_client->mutex));
	bool alreadyClosed = (a_client->connection && a_client->connection->status == DISCONNECTED);
	if(a_client->connection)
		a_client->connection->status = DISCONNECTED;
	pthread_mutex_unlock(&(a_client->mutex));

	cleanup_client(a_client);
	return !alreadyClosed;
}

/* Internal Methods */

void* ClientEpochThread(void *params){
	lsp_client *client = (lsp_client*)params;

	while(true){
		usleep(epoch_delay * 1000000); // convert seconds to microseconds
		if(DEBUG) printf("Client epoch handler waking up \n");

		// epoch is happening; send required messages
		if(client->connection->status == DISCONNECTED)
			break;

		if(client->connection->status == CONNECT_SENT){
			// connect sent already, but not yet acknowledged
			if(DEBUG) printf("Client resending connection request\n");
			rpc_send_conn_req(client);
		} else if(client->connection->status == CONNECTED){
			// send ACK for most recent message
			if(DEBUG) printf("Client acknowledging last received message: %d\n",client->connection->lastReceivedSeq);
			rpc_acknowledge(client->connection);

			// resend the first message in the outbox, if any
			if(client->connection->outbox.size() > 0) {
				if(DEBUG) printf("Client resending msg %d\n",client->connection->outbox.front()->seqnum);
				rpc_write(client->connection->clnt, *(client->connection->outbox.front()));
			}
		} else {
			if(DEBUG) printf("Unexpected client status: %d\n",client->connection->status);
		}

		pthread_mutex_lock(&(client->mutex));
		if(++(client->connection->epochsSinceLastMessage) >= num_epochs){
			// oops, we haven't heard from the server in a while;
			// mark the connection as disconnected
			if(DEBUG) printf("Too many epochs have passed since we heard from the server... disconnecting\n");
			client->connection->status = DISCONNECTED;
			break;
		}
		pthread_mutex_unlock(&(client->mutex));
	}
	pthread_mutex_unlock(&(client->mutex));
	if(DEBUG) printf("Epoch Thread exiting\n");
	return NULL;
}

// this write thread will ensure that messages can be sent/received faster than only
// on epoch boundaries. It will continuously poll for messages that are eligible to
// bet sent for the first time, and then send them out.
void* ClientWriteThread(void *params){
	lsp_client *client = (lsp_client*)params;

	// continuously poll for new messages to send;
	// Exit when the client is disconnected

	unsigned int lastSent = 0;

	while(true){
		pthread_mutex_lock(&(client->mutex));
		Status state = client->connection->status;

		if(state == DISCONNECTED)
			break;

		unsigned int nextToSend = client->connection->lastReceivedAck + 1;
		if(nextToSend > lastSent){
			// we have received an ack for the last message, and we haven't sent the
			// next one out yet, so if it exists, let's send it now
			if(client->connection->outbox.size() > 0) {
				rpc_write(client->connection->clnt, *(client->connection->outbox.front()));
				lastSent = client->connection->outbox.front()->seqnum;
			}
		}
		pthread_mutex_unlock(&(client->mutex));
		usleep(5000); // 5ms
	}
	pthread_mutex_unlock(&(client->mutex));
	return NULL;
}

void cleanup_client(lsp_client *client){
	// wait for threads to close
	void *status;
	if(client->rpcThread)
		pthread_join(client->rpcThread,&status);
	if(client->writeThread)
		pthread_join(client->writeThread,&status);
	if(client->epochThread)
		pthread_join(client->epochThread,&status);

	// cleanup the memory and connection
	pthread_mutex_destroy(&(client->mutex));
	cleanup_connection(client->connection);
	delete client;
}

void cleanup_connection(Connection *s){
	if(!s) return;
    // close the file descriptor and free memory
	rpc_destroy(s->clnt);
    delete s;
}

void* ClientRpcThread(void *params){
	lsp_client *client = (lsp_client*)params;

	assert (client->connection->id > 0);
	int prog_no = LSP_PROG + client->connection->id;

	register SVCXPRT *transp;

	pmap_unset (prog_no, LSP_VERS);

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL) {
		fprintf (stderr, "%s", "cannot create udp service.");
		exit(1);
	}
	if (!svc_register(transp, prog_no, LSP_VERS, lsp_prog_1, IPPROTO_UDP)) {
		fprintf (stderr, "%s", "unable to register (TEST_PROG, TEST_VERS, udp).");
		exit(1);
	}

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		fprintf (stderr, "%s", "cannot create tcp service.");
		exit(1);
	}
	if (!svc_register(transp, prog_no, LSP_VERS, lsp_prog_1, IPPROTO_TCP)) {
		fprintf (stderr, "%s", "unable to register (TEST_PROG, TEST_VERS, tcp).");
		exit(1);
	}

	if(DEBUG) printf ("Registered:: connid: %d, prog_no: %d\n", client->connection->id,
			prog_no);
	svc_run ();
	fprintf (stderr, "%s", "svc_run returned");
	return NULL;
}

// send a connection request
bool rpc_send_conn_req(lsp_client* client){
	message *msg = rpc_build_message(0, 0, NULL, 0);
	client->connection->status = CONNECT_SENT;
	int conn_id = rpc_write(client->connection->clnt, *msg);

	if(conn_id <= 0)
	{
		return (false);
	}

	client->connection->id = conn_id;
	return true;
}

int rpc_init(CLIENT* &clnt, const char* host)
{

	clnt = clnt_create(host, LSP_PROG, LSP_VERS, "udp");

	if (clnt == NULL) {
		clnt_pcreateerror(host);
		exit(1);
	}

	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	clnt_control(clnt, CLSET_TIMEOUT,(char*) &tv);

	return 0;
}

message* rpc_acknowledge(Connection *conn){
    message *msg = rpc_build_message(conn->id,conn->lastReceivedSeq,NULL,0);
    rpc_write(conn->clnt, *msg);
    return msg;
}

int rpc_write(CLIENT *clnt, message& outmsg)
{
	int ret_val = 0;
	timestamp_t t0 = get_timestamp();
	enum clnt_stat result = receive_1(&outmsg, &ret_val, clnt);	/* call the remote function */

	timestamp_t t1 = get_timestamp();
	double msecs = (t1 - t0) / 1000.0L;
	if(DEBUG) printf("Time taken for RPC call: %lf\n", msecs);

	/* test if the RPC succeeded */
	if (result != RPC_SUCCESS) {
		clnt_perror(clnt, "RPC Call failed on client:");
		if(DEBUG) {
			printf("outmsg: conn: %d, seqnum: %d pld: %s \n",
				outmsg.connid, outmsg.seqnum, outmsg.payload);
			printf("ret_val: %d\n", ret_val);
		}
	}
	else
		printf("rpc_write done: %d\n", ret_val);
	return ret_val;
}

int rpc_destroy(CLIENT *clnt)
{
	clnt_destroy( clnt );
	return 0;
}

int rpc_receive(message *msg)
{
	if(msg->connid == client_ptr->connection->id){
		pthread_mutex_lock(&(client_ptr->mutex));

		// reset counter for epochs since we have received a message

		client_ptr->connection->epochsSinceLastMessage = 0;

		if(strlen(msg->payload) == 0){
			// we received an ACK
			if(DEBUG) printf("Client received an ACK for msg %d\n",msg->seqnum);
			if(msg->seqnum == (client_ptr->connection->lastReceivedAck + 1)){
				// this sequence number is next in line, even if it overflows
				client_ptr->connection->lastReceivedAck = msg->seqnum;
			}
			if(client_ptr->connection->outbox.size() > 0 &&
					msg->seqnum == client_ptr->connection->outbox.front()->seqnum) {
				delete client_ptr->connection->outbox.front();
				client_ptr->connection->outbox.pop();
			}
		} else {
			// data packet
			if(DEBUG) printf("Client received msg %d\n",msg->seqnum);
			if(msg->seqnum == (client_ptr->connection->lastReceivedSeq + 1)){
				// next in the list
				client_ptr->connection->lastReceivedSeq++;
				message* msg_copy = rpc_build_message(msg);
				client_ptr->inbox.push(msg_copy);

				pthread_mutex_unlock(&(client_ptr->mutex));
				// send ack for this message
				rpc_acknowledge(client_ptr->connection);

				pthread_mutex_lock(&(client_ptr->mutex));
			}
		}

		pthread_mutex_unlock(&(client_ptr->mutex));
	}
	return 0;
}

bool_t receive_1_svc(message *msg, int* result, struct svc_req *rqstp)
{
	if(DEBUG) printf("Received on client: conn: %d, seqnum: %d pld: %s \n",
			msg->connid, msg->seqnum, msg->payload);

	*result = rpc_receive(msg);
	return true;
}

