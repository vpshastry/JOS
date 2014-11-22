#include <kern/e1000.h>
#include <kern/pci.h>
#include <kern/pmap.h>
#include <inc/string.h>
// LAB 6: Your driver code here

//Declare Transmit Description Array
struct tx_desc new_tx_desc_arr[64] __attribute__((aligned(16)));
char new_pkt_bufs_arr[64][1518];
volatile uint32_t *e1000_virtual_base;

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
	e1000_virtual_base = (uint32_t *) mmio_map_region (func->reg_base[0],
							   func->reg_size[0]);

	cprintf("\nVerify Value 0x80080783:%x\n",
				e1000_virtual_base[get_arr_index(0x0008)]);

	//Transmit Initialization
	for (i = 0; i < 64; i++) {
		new_tx_desc_arr[i].addr = PADDR(new_pkt_bufs_arr[i]);

		//SET RS Bit to 1
		new_tx_desc_arr[i].cmd = 0x08;

		new_tx_desc_arr[i].status = 0x01;

	}

	//Initialize the values of TDBAL AND TDBAH
	e1000_virtual_base[E1000_TDBAL/4] = PADDR(new_tx_desc_arr);

	//INITIALIZE TDLEN
	length = sizeof(struct tx_desc) * 64;
	e1000_virtual_base[E1000_TDLEN/4] = length;

	//INITIALIZE TDH AND TDT
	e1000_virtual_base[E1000_TDH/4] = 0x0;
	e1000_virtual_base[E1000_TDT/4] = 0x0;

	//INITIALIZE TCTL

	//SET EN to 1, SET PSP to 1, CT to 10h, COLD to 40h, IGP/TIGP to 10
	e1000_virtual_base[E1000_TCTL/4];

	e1000_virtual_base[E1000_TCTL/4] |= E1000_TCTL_EN;
	e1000_virtual_base[E1000_TCTL/4] |= E1000_TCTL_PSP;
	e1000_virtual_base[E1000_TCTL/4] |= 0x00000100;
	e1000_virtual_base[E1000_TCTL/4] |= 0x00040000;

	cprintf("\nValue of TCTL.. %d \n", e1000_virtual_base[E1000_TCTL/4]);

	e1000_virtual_base[E1000_TIPG/4] |= 0x60200a;

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
	if(len > 1518){
		panic("Big Packet, Cant Transmit\n");
	}
	memmove(new_pkt_bufs_arr[i], pkt, len);

	new_tx_desc_arr[i].length = len;

	/* Debug logs
	cprintf("Length:%d \n",new_tx_desc_arr[i].length);
	cprintf("CSO:%d \n",new_tx_desc_arr[i].cso);
	cprintf("CMD:%d \n",new_tx_desc_arr[i].cmd);
	cprintf("pkt:%s \n\n",pkt);
	*/

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
