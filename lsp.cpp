#include "lsp.h"
int lsp_prog_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
    xdr_free (xdr_result, result);

    /*
     * Insert additional freeing code here, if needed
     */

    return 1;
}

message* rpc_build_message(const message* msg)
{
	return rpc_build_message(msg->connid, msg->seqnum,
			(uint8_t *) msg->payload, strlen(msg->payload) + 1);
}

message* rpc_build_message(int id, int seq, uint8_t *pld, int len)
{
    message *msg = new message();

    msg->connid = id;
    msg->seqnum = seq;
    memcpy(msg->payload, pld, len);

    return msg;
}

timestamp_t get_timestamp ()
{
  struct timeval now;
  gettimeofday (&now, NULL);
  return  now.tv_usec + (timestamp_t)now.tv_sec * 1000000;
}
