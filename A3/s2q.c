#include "sim.h"
#include "coremap.h"
#define UNUSED(x) (void)(x)

list_head A1 = {{NULL,NULL}};
list_head AM = {{NULL,NULL}};
int threshold;
int size;
/* Page to evict is chosen using the simplified 2Q algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int s2q_evict(void)
{
	int res;
	list_entry* evicted;
	if (size > threshold) {
		evicted = list_first_entry(&A1);
		struct frame *f = container_of(evicted, struct frame, framelist_entry);
		size--;
		res = f - coremap;
	} else {
		evicted = list_last_entry(&AM);
		struct frame *f = container_of(evicted, struct frame, framelist_entry);
		res = f - coremap;
		set_referenced(coremap[res].pte, false);
	}

	list_del(evicted);
	return res;
}

/* This function is called on each access to a page to update any information
 * needed by the simplified 2Q algorithm.
 * Input: The page table entry and full virtual address (not just VPN)
 * for the page that is being accessed.
 */
void s2q_ref(int frame, vaddr_t vaddr)
{
	UNUSED(vaddr);

	list_entry *evited = &coremap[frame].framelist_entry;
	if (list_entry_is_linked(evited))
	{
		if (get_referenced(coremap[frame].pte))
		{
			list_del(evited);
			list_add_head(&AM, evited);
		} else
		{
			set_referenced(coremap[frame].pte, 1);
			list_del(evited);
 			list_add_head(&AM, evited);
			size--;
		}
	}
	else
	{
		list_add_tail(&A1, evited);
		size++;
	}
}

/* Initialize any data structures needed for this replacement algorithm. */
void s2q_init(void)
{
	size = 0;
	threshold = memsize / 10;
	list_init(&A1);
	list_init(&AM);
}

/* Cleanup any data structures created in s2q_init(). */
void s2q_cleanup(void)
{
	list_destroy(&A1);
	list_destroy(&AM);
}
