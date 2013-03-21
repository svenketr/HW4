// John Keech, UIN:819000713, CSCE 438 - HW2

#include "network.h"

double drop_rate = _DROP_RATE;

#define PACKET_SIZE 2048

Connection* network_setup_server(int port){
    Connection *conn = new Connection();
    
    // create the socket
    if((conn->fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1){
        printf("Error creating the server socket\n");
        delete conn;
        return NULL;
    }
    
    // setup local address to bind on
    conn->addr = new sockaddr_in();
    memset(conn->addr,0,sizeof(sockaddr_in));
    conn->addr->sin_family = AF_INET;
    conn->addr->sin_port = htons(port);
    conn->addr->sin_addr.s_addr = htonl(INADDR_ANY);
    
    // bind the socket
    if(bind(conn->fd,(sockaddr*)conn->addr,sizeof(sockaddr_in)) == -1){
        printf("Error binding socket to port %d\n",port);
        delete conn->addr;
        delete conn;
        return NULL;
    }
    
    return conn;
}

Connection* network_make_connection(const char *host, int port){
    // creates a socket to the specified host and port
    
    int sd = socket(AF_INET, SOCK_DGRAM, 0);
    
    if(sd < 0) {
        printf("Error creating socket: %d\n",sd);
        return NULL;
    }
    
    // fill in the sockaddr_in structure
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    char server[256];
    
    if(host)
        strcpy(server,host);
    else
        strcpy(server,"127.0.0.1");
        
    memset(&addr,0,addrlen);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(server);
    
    // not an IP, so try to look it up as a hostname
    if(addr.sin_addr.s_addr == (unsigned long)INADDR_NONE){
        struct hostent *hostp = gethostbyname(server);
        if(!hostp){
            printf("could not find host %s\n",server);
            return NULL;
        }
        memcpy(&addr.sin_addr, hostp->h_addr, sizeof(addr.sin_addr));
    }
    
    // the socket was built successfully, so now copy that info to the connection object
    Connection *c = new Connection();
    c->fd = sd;
    c->addr = new sockaddr_in();
    memcpy(c->addr,&addr,addrlen);
    return c;   
}

// send a connection request
bool network_send_connection_request(Connection *conn){
    LSPMessage *msg = network_build_message(0,0,NULL,0);    
    if(network_send_message(conn,msg)) {
        conn->status = CONNECT_SENT;
        return true;
    } else {
        return false;
    }
}

// look for an acknowledgement of a connection request, and retrieve the
// connection ID from the response
bool network_wait_for_connection(Connection *conn, double timeout){
    sockaddr_in addr;
    LSPMessage *msg = network_read_message(conn, timeout,&addr);
    if (msg && msg->seqnum() == 0){
        conn->id = msg->connid();
        if(DEBUG) printf("[%d] Connected\n",conn->id); 
        return true;
    } else {
        if(DEBUG) printf("Timed out waiting for connection from server after %.2f seconds\n",timeout);
        return false;
    }
}

// acknowledge the last received message
bool network_acknowledge(Connection *conn){
    LSPMessage *msg = network_build_message(conn->id,conn->lastReceivedSeq,NULL,0);
    return network_send_message(conn,msg);
}

bool network_send_message(Connection *conn, LSPMessage *msg){
    // sends an LSP Message
    
    if(DEBUG) printf("Sending message (%d,%d,\"%s\")\n",msg->connid(),msg->seqnum(),msg->payload().c_str());

    // marshal the message into a buffer for sending
    int len;
    uint8_t* buf = network_marshal(msg,&len);
    
    if(sendto(conn->fd, buf, len, 0, (sockaddr*)(conn->addr), sizeof(sockaddr_in)) == -1){
        printf("Error sending message: %s\n",strerror(errno));
        return false;
    }
    
    // free up the memory
    delete buf;
    return true;
}

LSPMessage* network_read_message(Connection *conn, double timeout, sockaddr_in *addr){
    // attmpts to read a message from a Connection. Returns the message if one was read,
    // or it returns NULL if the timeout was reached before a message was received.
    
    fd_set read;
    FD_ZERO(&read);
    FD_SET(conn->fd, &read);
    
    timeval t = network_get_timeval(timeout);
    while(true){
        int result = select(conn->fd + 1, &read, NULL, NULL, &t);
        if(result == -1){
            printf("Error receiving message: %s\n",strerror(errno));
            return NULL;
        } else if (result == 0) {
            if(DEBUG) printf("Receive timed out after %.2f seconds\n",timeout);
            return NULL;
        } else {
            // a packet was received, let's parse it
            uint8_t buf[PACKET_SIZE];
            socklen_t addr_len = sizeof(sockaddr_in);
            result = recvfrom(conn->fd, buf, PACKET_SIZE, 0, (sockaddr*)addr, &addr_len);
            if (result == -1 || addr_len != sizeof(sockaddr_in)){
                printf("Error reading response: %s\n",strerror(errno));
                return NULL;
            }
        
            if(network_should_drop())
                continue; // drop the packet and continue reading
        
            return network_unmarshal(buf,result);
        }
    }
}

LSPMessage* network_build_message(int id, int seq, uint8_t *pld, int len){
    // create the LSPMessage data structure and fill it in
    
    LSPMessage *msg = new LSPMessage();
    
    msg->set_connid(id);
    msg->set_seqnum(seq);
    msg->set_payload(pld,len);
    
    return msg;
}

uint8_t* network_marshal(LSPMessage *msg, int *packedSize){
    // serialize LSPMessage to byte array
    int len = msg->ByteSize();
    uint8_t *buf = new uint8_t[len];
    msg->SerializeToArray(buf, len);
    
    *packedSize = len;
    return buf;
}

LSPMessage* network_unmarshal(uint8_t *buf, int buf_len){
    // unpack LSPMessage into the various components
    LSPMessage *msg = new LSPMessage();
    if(!msg->ParseFromArray(buf,buf_len)){
        printf("Error unpacking LSPMessage\n");
        delete msg;
        return NULL;
    }
    
    return msg;
}

// configure the network drop rate
void network_set_drop_rate(double percent){
    if(percent >= 0 && percent <= 1)
        drop_rate = percent;
}

// use the drop rate to determine if we should send/receive this packet
bool network_should_drop(){
    return (rand() / (double)RAND_MAX) < drop_rate;
}

struct timeval network_get_timeval(double seconds){
    // create the timeval structure needed for the timeout in the select call for reading from a socket
    timeval t;
    t.tv_sec = (long)seconds;
    t.tv_usec = (seconds - (long)seconds) * 1000000; // remainder in s to us
    return t;
}

