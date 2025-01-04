#include "sim.h"
#include "coremap.h"
#define UNUSED(x) (void)(x)
/* Page to evict is chosen using the CLOCK algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int clock_hand = 0;

int clock_evict(void)
{
	int ret = -1;
	while (get_referenced(coremap[clock_hand].pte)) 
	{
		set_referenced(coremap[clock_hand].pte, 0);
		clock_hand++; 
		clock_hand = clock_hand % memsize;
	}
	ret = clock_hand;
	clock_hand++; 
	clock_hand = clock_hand % memsize;
	return ret;
}

/* This function is called on each access to a page to update any information
 * needed by the CLOCK algorithm.
 * Input: The page table entry and full virtual address (not just VPN)
 * for the page that is being accessed.
 */
void clock_ref(int frame, vaddr_t vaddr)
{
	UNUSED(vaddr);
	set_referenced(coremap[frame].pte, 1);
}

/* Initialize any data structures needed for this replacement algorithm. */
void clock_init(void)
{
	clock_hand = 0;
}

/* Cleanup any data structures created in clock_init(). */
void clock_cleanup(void)
{

}
