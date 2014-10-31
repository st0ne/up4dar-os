/*

Copyright (C) 2012   Michael Dirska, DL1BFF (dl1bff@mdx.de)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


/*
 * eth_txmem.c
 *
 * Created: 10.05.2012 09:08:32
 *  Author: mdirska
 */ 


#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"


#include "eth_txmem.h"


#define NUM_MEM_CFG 2
#define TX_BUFFER_Q_LEN		15

static unsigned long tx_buffer_q[TX_BUFFER_Q_LEN * 2];


static eth_txmem_t * txmem_pool[NUM_MEM_CFG];


#define TXMEM_FREE  1
#define TXMEM_ALLOC 2
#define TXMEM_IN_TXQ 3
#define TXMEM_IN_HARDWARE_Q 4

static xQueueHandle  tx_q;

int eth_txmem_init(void)
{
	AVR32_MACB.tbqp = (unsigned long) & tx_buffer_q;
	
	tx_q = xQueueCreate( 10, sizeof (eth_txmem_t *) );

	int i;
	for (i=0; i < NUM_MEM_CFG; i++)
	{
		txmem_pool[i]->state = TXMEM_FREE;
	}


	return 0;
}

static void eth_txmem_cleanup (void)
{
	int i;
	for (i=0; i < NUM_MEM_CFG; i++)
	{
		if (txmem_pool[i]->state == TXMEM_IN_HARDWARE_Q)
		{
			txmem_pool[i]->state = TXMEM_FREE;
			vPortFree(txmem_pool[i]->data);
			vPortFree(txmem_pool[i]);
		}
	}
}


void eth_txmem_free (eth_txmem_t * packet)
{
	packet->state = TXMEM_FREE;
	vPortFree(packet->data);
	vPortFree(packet);
}

eth_txmem_t * eth_txmem_get (int size)
{
	int i;
	for (i=0; i < NUM_MEM_CFG; i++)
	{
		if (txmem_pool[i]->state == TXMEM_FREE)
		{
			uint8_t * data = (uint8_t *) pvPortMalloc(sizeof(eth_txmem_t));
			if (data == NULL)
			{
				return 0;
			}
			
			txmem_pool[i] = pvPortMalloc(size);
			if (txmem_pool[i] == NULL)
			{
				vPortFree(data);
				return 0;
			}
			
			txmem_pool[i]->data = data;
			txmem_pool[i]->state = TXMEM_ALLOC;
			txmem_pool[i]->tx_size = 0;
			
			return txmem_pool[i];
		}
	}
		
	return 0;
}


int eth_txmem_send (eth_txmem_t * packet)
{
	
	eth_txmem_t * p = packet;
	
	p->state = TXMEM_IN_TXQ;
	
	if( xQueueSend( tx_q, &p, 0 ) != pdPASS )
	{
		p->state = TXMEM_FREE; 
		vPortFree(packet->data);
		vPortFree(packet);
		return -1;
	}		
	
	return 0;
}


static int eth_tx_active = 0;

void eth_txmem_flush_q (void)
{
	int count = 0;
	
	if (eth_tx_active != 0)
	{
		if ((AVR32_MACB.tsr & 0x08) != 0) // TGO set -> TX active
		{
			return;  // don't write anything to tx_buffer_q while MACB is working
		}
		else
		{
			eth_tx_active = 0;
			eth_txmem_cleanup(); // free used memory
		}
	}
	
	
	
	while(count < TX_BUFFER_Q_LEN)
	{
		eth_txmem_t * p;
		
		if( xQueueReceive( tx_q, &p, 0 ) != pdPASS)
			break;  // queue is empty
			
		tx_buffer_q[ (count << 1) + 0 ] = (unsigned long) p->data;
		tx_buffer_q[ (count << 1) + 1 ] = ((unsigned long) p->tx_size) | 0x00008000; // last buffer of this frame
		
		p->state = TXMEM_IN_HARDWARE_Q;
		
		count ++;
	}
	
	
	
	if (count > 0)
	{
		AVR32_MACB.tbqp = (unsigned long) & tx_buffer_q; // reset buffer and internal pointer
		
		tx_buffer_q[ ((count - 1) << 1) + 1 ] |= 0x40000000; // set wrap bit on last buffer
		AVR32_MACB.NCR.tstart = 1; // transmit frames
		
		eth_tx_active = 1;
	}
	
	// extern int debug1;
	// debug1 = eth_tx_active;
}
