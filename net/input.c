#include "ns.h"
#include <inc/lib.h>

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.


	char *data=NULL;


	int length = sys_receive_packet_e1000(data);
	if(length == -1){
		panic("Something wrong with recieve packet");
	}
	
	memmove(nsipcbuf.pkt.jp_data,data,length);

	ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P|PTE_W|PTE_U);


}
