#include "lsp.h"
int lsp_prog_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
    xdr_free (xdr_result, result);

    /*
     * Insert additional freeing code here, if needed
     */

    return 1;
}

message* rpc_acknowledge(Connection *conn){
    message *msg = rpc_build_message(conn->id,conn->lastReceivedSeq,NULL,0);
    return msg;
}

message* rpc_build_message(int id, int seq, uint8_t *pld, int len)
{
    message *msg = new message();

    msg->connid = id;
    msg->seqnum = seq;
    memcpy(msg->payload, pld, len);

    return msg;
}
