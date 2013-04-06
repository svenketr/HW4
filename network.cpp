// John Keech, UIN:819000713, CSCE 438 - HW2

#include "network.h"

double drop_rate = _DROP_RATE;

#define PACKET_SIZE 2048
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

