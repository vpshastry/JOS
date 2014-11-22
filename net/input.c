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


	int result=0;

	while (1) {
		sys_page_alloc(0, &nsipcbuf,PTE_U | PTE_P | PTE_W);

		result = sys_receive_packet_e1000(nsipcbuf.pkt.jp_data);

		unsigned now = sys_time_msec();
		unsigned end = now + 100 * 1;

		if(result == -1){
			while(sys_time_msec() <end)
				sys_yield();

			continue;
		}

		nsipcbuf.pkt.jp_len=result;

		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P | PTE_W | PTE_U);

		while(sys_time_msec() <end)
			sys_yield();
	}
}


