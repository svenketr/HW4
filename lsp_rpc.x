struct message {
	int connid;
	int seqnum;
};


program SERVER_PROG {
	version SERVER_VERS {
		int receive(message)  = 1;
		message send(int connid) = 2;
	} = 1;
} = 0x33388858;	
