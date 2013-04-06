// John Keech, UIN:819000713, CSCE 438 - HW2

#ifndef NETWORK_HANDLER
#define NETWORK_HANDLER

#include "datatypes.h"

#define _DROP_RATE 0.0
// configure the network drop rate
void network_set_drop_rate(double percent);

// use the drop rate to determine if we should send/receive this packet
bool network_should_drop();

// create a timeval structure
struct timeval network_get_timeval(double seconds);

#endif
