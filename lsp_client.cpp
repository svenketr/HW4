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
void convert_lspmsg2msg(LSPMessage* lspmsg, message* msg)
{
	msg->connid = lspmsg->connid();
	msg->seqnum = lspmsg->seqnum();
	strcpy(msg->payload, lspmsg->payload().c_str());
}

lsp_client* lsp_client_create(const char* dest, int port){
    lsp_client *client = new lsp_client();
    pthread_mutex_init(&(client->mutex),NULL);
    client->connection = network_make_connection(dest,port);
    
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

    if(network_send_connection_request(client->connection) && 
       network_wait_for_connection(client->connection, epoch_delay * num_epochs)){
        pthread_mutex_lock(&(client->mutex));
        
        // connection succeeded, build lsp_client struct        
        client->connection->port = port;
        client->connection->host = dest;
        client->connection->status = CONNECTED;
        
        // kick off ReadThread to catch incoming messages
        int res;
        if((res = pthread_create(&(client->readThread), NULL, ClientReadThread, (void*)client)) != 0){
            printf("Error: Failed to start the read thread: %d\n",res);
            lsp_client_close(client);
            return NULL;
        }
        if((res = pthread_create(&(client->writeThread), NULL, ClientWriteThread, (void*)client)) != 0){
            printf("Error: Failed to start the write thread: %d\n",res);
            lsp_client_close(client);
            return NULL;
        }
        
        rpc_init(client->connection->clnt, dest);

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
        LSPMessage *msg = NULL;
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
            std::string payload = msg->payload();
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
    LSPMessage *msg = network_build_message(a_client->connection->id,a_client->connection->lastSentSeq,pld,lth);
    
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
        pthread_mutex_lock(&(client->mutex));
        if(client->connection->status == DISCONNECTED)
            break;
        
        if(client->connection->status == CONNECT_SENT){
            // connect sent already, but not yet acknowledged
            if(DEBUG) printf("Client resending connection request\n");
            network_send_connection_request(client->connection);
        } else if(client->connection->status == CONNECTED){
            // send ACK for most recent message
            if(DEBUG) printf("Client acknowledging last received message: %d\n",client->connection->lastReceivedSeq);
            network_acknowledge(client->connection);
            
            // resend the first message in the outbox, if any
            if(client->connection->outbox.size() > 0) {
                if(DEBUG) printf("Client resending msg %d\n",client->connection->outbox.front()->seqnum());
                rpc_send_message(client->connection->clnt, client->connection->outbox.front());
                network_send_message(client->connection,client->connection->outbox.front());
            }
        } else {
            if(DEBUG) printf("Unexpected client status: %d\n",client->connection->status);
        }
        
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

void* ClientReadThread(void *params){
    lsp_client *client = (lsp_client*)params;
    
    // continuously poll for new messages and process them;
    // Exit when the client is disconnected
    while(true){
        pthread_mutex_lock(&(client->mutex));
        Status state = client->connection->status;
        pthread_mutex_unlock(&(client->mutex));
        
        if(state == DISCONNECTED)
            break;
        
        // attempt to read
        sockaddr_in addr;
        message* _msg = rpc_read(client->connection->clnt, client->connection->id);

        LSPMessage *msg = network_read_message(client->connection, 0.5,&addr);
        if(msg) {
            if(msg->connid() == client->connection->id){
                pthread_mutex_lock(&(client->mutex));
                
                // reset counter for epochs since we have received a message
                client->connection->epochsSinceLastMessage = 0;
                
                if(msg->payload().length() == 0){
                    // we received an ACK
                    if(DEBUG) printf("Client received an ACK for msg %d\n",msg->seqnum());
                    if(msg->seqnum() == (client->connection->lastReceivedAck + 1)){
                        // this sequence number is next in line, even if it overflows
                        client->connection->lastReceivedAck = msg->seqnum();
                    }
                    if(client->connection->outbox.size() > 0 && msg->seqnum() == client->connection->outbox.front()->seqnum()) {
                        delete client->connection->outbox.front();
                        client->connection->outbox.pop();
                    }
                } else {
                    // data packet
                    if(DEBUG) printf("Client received msg %d\n",msg->seqnum());
                    if(msg->seqnum() == (client->connection->lastReceivedSeq + 1)){
                        // next in the list
                        client->connection->lastReceivedSeq++;
                        client->inbox.push(msg);
                        
                        // send ack for this message
                        network_acknowledge(client->connection);
                    }
                }
                
                pthread_mutex_unlock(&(client->mutex));
            }
        }
    }
    if(DEBUG) printf("Read Thread exiting\n");
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
            	rpc_send_message(client->connection->clnt, client->connection->outbox.front());
                network_send_message(client->connection,client->connection->outbox.front());
                lastSent = client->connection->outbox.front()->seqnum();
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
    if(client->readThread)
        pthread_join(client->readThread,&status);
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
    if(s->fd != -1)
        close(s->fd);
    delete s->addr;
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

	svc_run ();
	fprintf (stderr, "%s", "svc_run returned");
	return NULL;
}

// send a connection request
bool rpc_send_conn_req(lsp_client* client){
    LSPMessage *msg = network_build_message(0, 0, NULL, 0);
    if(network_send_message(client->connection,msg)) {
    	client->connection->status = CONNECT_SENT;
        return true;
    } else {
        return false;
    }

    int res;
	if((res = pthread_create(&(client->rpcThread), NULL, ClientRpcThread, (void*)client)) != 0){
		printf("Error: Failed to start the rpc thread: %d\n",res);
		lsp_client_close(client);
		return NULL;
	}

}

int rpc_init(CLIENT* &clnt, const char* host)
{
	clnt = clnt_create(host, LSP_PROG, LSP_VERS, "udp");
	if (clnt == NULL) {
		clnt_pcreateerror(host);
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

	if(DEBUG) printf("Received on client: conn: %d, seqnum: %d \n", msg->connid, msg->seqnum);


	return &result;
}

