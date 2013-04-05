struct message {
	int connid;
	int seqnum;
	char payload[1000];
};


program LSP_PROG {
	version LSP_VERS {
		int receive(message) = 1;
	} = 1;
} = 0x33278858;	
