#include "ns.h"
#include <inc/lib.h>
extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver

	envid_t *env_store = NULL;
	int *perm_store = NULL;
	int r = 0;
	int value = 0;

	cprintf("IN OUTPUT\n");

	while (1) {
		value = ipc_recv (env_store, &nsipcbuf, perm_store);
		if (thisenv->env_ipc_from != ns_envid)
			continue;

		r = sys_transmit_packet_e1000 (nsipcbuf.pkt.jp_data,
							nsipcbuf.pkt.jp_len);
		if (r < 0)
			panic("Transmit failure");
	}
	return;
}

