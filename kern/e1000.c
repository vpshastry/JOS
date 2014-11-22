#include <kern/e1000.h>
#include <kern/pci.h>
#include <kern/pmap.h>
#include <inc/string.h>
// LAB 6: Your driver code here



//Declare Transmit Description Array
struct tx_desc new_tx_desc_arr[64] __attribute__((aligned(16)));
char new_pkts_arr[64][2048];
volatile uint32_t *e1000_virtual_base;


//Recieve Descriptor Array
struct rx_desc new_rx_desc_arr[64] __attribute__((aligned(16)));
char recv_pkts[64][2048];


//Convert offset to array index
int
get_arr_index(int32_t Const)
{
	return Const/4;
}

//Attach function
int
pci_enable_e1000_attach(struct pci_func *func)
{
	int i;
	int length = 0;

	pci_func_enable(func);

	if (!func->reg_base[0])
		panic("Reg Base 0");

	//Map Virtual addresses for the E1000 physical addresses
	e1000_virtual_base = (uint32_t *) mmio_map_region (ROUNDDOWN(func->reg_base[0],PGSIZE),
							   ROUNDUP(func->reg_size[0],PGSIZE));

	cprintf("\nVerify Value 0x80080783:%x\n",
				e1000_virtual_base[get_arr_index(0x0008)]);

	//Transmit Initialization
	for (i = 0; i < 64; i++) {
		new_tx_desc_arr[i].addr = PADDR(new_pkts_arr[i]);

		//SET RS Bit to 1
		new_tx_desc_arr[i].cmd = 0x08;

		new_tx_desc_arr[i].status = 0x01;

	}

	//Initialize the values of TDBAL AND TDBAH
	e1000_virtual_base[E1000_TDBAL/4] =(uint64_t)PADDR(new_tx_desc_arr);
	e1000_virtual_base[E1000_TDBAH/4] = 0x0;

	//INITIALIZE TDLEN
	length = sizeof(struct tx_desc) * 64;
	e1000_virtual_base[E1000_TDLEN/4] = length;

	//INITIALIZE TDH AND TDT
	e1000_virtual_base[E1000_TDH/4] = 0x0;
	e1000_virtual_base[E1000_TDT/4] = 0x0;

	//INITIALIZE TCTL
	e1000_virtual_base[E1000_TCTL/4] |= E1000_TCTL_EN;
	e1000_virtual_base[E1000_TCTL/4] |= E1000_TCTL_PSP;
	e1000_virtual_base[E1000_TCTL/4] |= 0x00000100;
	e1000_virtual_base[E1000_TCTL/4] |= 0x00040000;


	e1000_virtual_base[E1000_TIPG/4] |= 0x60200a;
	/********************************************************************
			TRANSMISSION ENDS HERE
	********************************************************************/

	//Recieve Initialization
	for(i=0;i<64;i++){
		new_rx_desc_arr[i].addr = PADDR(recv_pkts[i]);
		new_rx_desc_arr[i].status = 0x0; //Initially make DD and EOP 0
	}

	e1000_virtual_base[E1000_RA/4] = 0x12005452;
	e1000_virtual_base[(E1000_RA+4)/4] = 0x5634 | E1000_RAH_AV;

	e1000_virtual_base[E1000_MTA/4]=0x0;

	e1000_virtual_base[E1000_RDBAL/4]=PADDR(new_rx_desc_arr);
	e1000_virtual_base[E1000_RDBAH/4]=0x0;

	//INITIALIZE RDLEN
	length = sizeof(struct rx_desc) * 64;
	e1000_virtual_base[E1000_RDLEN/4] = length;


	//INITIALIZE RDH AND RDT
	e1000_virtual_base[E1000_RDH/4] = 0x0;
	e1000_virtual_base[E1000_RDT/4] = 0x0;

	e1000_virtual_base[E1000_RCTL/4] = 0x4008002;

	return 0;
}

int
transmit_packet_e1000 (char *pkt, int len)
{
	cprintf("\nIN TRANSMIT PACKET\n");

	uint32_t i = e1000_virtual_base[E1000_TDT/4];

	while((new_tx_desc_arr[i].status & 0x01) == 0 ){
		i = (i+1)%64;
	}


	//Copy Pft data into buffer
	if(len > 2048){
		panic("Big Packet, Cant Transmit\n");
	}
	memmove(new_pkts_arr[i], pkt, len);

	new_tx_desc_arr[i].length = len;


	// REMOVE THE DD BIT INITIALIZED, RESET TO 0.
	// THE SET RS BIT WILL AUTOMATICALLY UPDATE IT TO 1 AFTER the
	// TRANSMISSION IS COMPLETE.
	new_tx_desc_arr[i].status &= ~0x1;

	// SET EOP  Bit to 1
	new_tx_desc_arr[i].cmd |= 0x01;

	//SET TDT
	e1000_virtual_base[E1000_TDT/4] = (i+1)%64;

	return 0;

}


int
receive_packet_e1000(char *pkt){

	uint32_t rdt, next;

	rdt = e1000_virtual_base[E1000_RDT/4];

	next = (rdt+1) % 64;

	int stat = new_rx_desc_arr[rdt].status;

	int status = (new_rx_desc_arr[rdt].status & 0x1);

	if(status != 0x1){
		return -1;
	}

	int len = new_rx_desc_arr[rdt].length;

	if(len > 2048){
		panic("Big Packet, Cant Receive\n");
		return -1;
	}

	memmove(pkt,recv_pkts[rdt],len);

	//REMOVE THE DD BIT AND EOP INITIALIZED, RESET TO 0.
	new_rx_desc_arr[rdt].status &=(~ 0x1);

	//new_rx_desc_arr[rdt].status = 0x0;

	*(e1000_virtual_base + E1000_RDT/4) = next;

	return len;
}
