/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Andrew Peterson, Karen Reid, Alexey Khrabrov, Angela Brown, Kuei Sun
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019, 2021 Karen Reid
 * Copyright (c) 2023, Angela Brown, Kuei Sun
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "malloc369.h"
#include "sim.h"
#include "coremap.h"
#include "swap.h"
#include "pagetable.h"

const unsigned int mask_12bits = 0xFFF;
const unsigned int mask_dirty_bit = 0x2;
const unsigned int mask_ref_bit = 0x1;
const unsigned int mask_valid_bit = 0x4;
const unsigned int bit_shift = 3;
// Counters for various events.
// Your code must increment these when the related events occur.
size_t hit_count = 0;
size_t miss_count = 0;
size_t ref_count = 0;
size_t evict_clean_count = 0;
size_t evict_dirty_count = 0;

pd first_pd[PAGE_SIZE];
// Accessor functions for page table entries, to allow replacement
// algorithms to obtain information from a PTE, without depending
// on the internal implementation of the structure.

/* Returns true if the pte is marked valid, otherwise false */
bool is_valid(pt_entry_t *pte)
{
	return pte->frame & mask_valid_bit;
}

/* Returns true if the pte is marked dirty, otherwise false */
bool is_dirty(pt_entry_t *pte)
{
	return pte->frame & mask_dirty_bit;
}

/* Returns true if the pte is marked referenced, otherwise false */
bool get_referenced(pt_entry_t *pte)
{
	return pte->frame & mask_ref_bit;
}

/* Sets the 'referenced' status of the pte to the given val */
void set_referenced(pt_entry_t *pte, bool val)
{
	if (val) pte->frame |= mask_ref_bit;
	else pte->frame &= ~mask_ref_bit;
}

/*
 * Initializes your page table.
 * This function is called once at the start of the simulation.
 * For the simulation, there is a single "process" whose reference trace is
 * being simulated, so there is just one overall page table.
 *
 * In a real OS, each process would have its own page table, which would
 * need to be allocated and initialized as part of process creation.
 * 
 * The format of the page table, and thus what you need to do to get ready
 * to start translating virtual addresses, is up to you. 
 */
void init_pagetable(void)
{
	for (int i = 0; i < PAGE_SIZE; i ++) 
	{
		first_pd[i].next_level = NULL;
		first_pd[i].real_pt = NULL;
	}
}

pd* init_second_level(void)
{
	pd* res = malloc369(sizeof(pd) * PAGE_SIZE);
	for (int i = 0; i < PAGE_SIZE; i ++) 
	{
		res[i].next_level = NULL;
		res[i].real_pt = NULL;
	}
	return res;
}

pt_entry_t* init_thrid_level(void)
{
	pt_entry_t* res = malloc369(sizeof(pt_entry_t) * PAGE_SIZE);
	for (int i = 0; i < PAGE_SIZE; i++)
	{
		res[i].frame = 0;
		res[i].offt = INVALID_SWAP;
	}
	return res;
}

/*
 * Write virtual page represented by pte to swap, if needed, and update 
 * page table entry.
 *
 * Called from allocate_frame() in coremap.c after a victim page frame has
 * been selected. 
 *
 * Counters for evictions should be updated appropriately in this function.
 */
void handle_evict(pt_entry_t * pte)
{
	if (pte->frame & mask_dirty_bit) 
	{	
		evict_dirty_count ++;
		off_t offst = swap_pageout(pte->frame >> bit_shift, pte->offt);
		pte->offt = offst;
		pte->frame = (pte->frame & ~mask_valid_bit & ~mask_dirty_bit);
	}
	else 
	{
		pte->frame = (pte->frame & ~mask_valid_bit & ~mask_dirty_bit);
		evict_clean_count ++;
	}
}

/*
 * Locate the physical frame number for the given vaddr using the page table.
 *
 * If the page table entry is invalid and not on swap, then this is the first 
 * reference to the page and a (simulated) physical frame should be allocated 
 * and initialized to all zeros (using init_frame from coremap.c).
 * If the page table entry is invalid and on swap, then a (simulated) physical 
 * frame should be allocated and filled by reading the page data from swap.
 *
 * Make sure to update page table entry status information:
 *  - the page table entry should be marked valid
 *  - if the type of access is a write ('S'tore or 'M'odify),
 *    the page table entry should be marked dirty
 *  - a page should be marked dirty on the first reference to the page,
 *    even if the type of access is a read ('L'oad or 'I'nstruction type).
 *  - DO NOT UPDATE the page table entry 'referenced' information. That
 *    should be done by the replacement algorithm functions.
 *
 * When you have a valid page table entry, return the page frame number
 * that holds the requested virtual page.
 *
 * Counters for hit, miss and reference events should be incremented in
 * this function.
 */
int find_frame_number(vaddr_t vaddr, char type)
{
	// To keep compiler happy - remove when you have a real use
	unsigned int f_pd = (vaddr >> 36) & mask_12bits;
	unsigned int s_pd = (vaddr >> 24) & mask_12bits;
	unsigned int t_pd = (vaddr >> 12) & mask_12bits;
	// if secondary page directory does not exisit, create it.
	if (!(first_pd[f_pd].next_level))  first_pd[f_pd].next_level = init_second_level();
	// getting secondary page_directory
	pd* second_pd = first_pd[f_pd].next_level;
	// if thrid level does not exist we malloc an array of PTEs.
	if (!(second_pd[s_pd].real_pt)) second_pd[s_pd].real_pt = init_thrid_level();
	// getting thrid level PageTable.
	pt_entry_t* thrid_pt = second_pd[s_pd].real_pt;
	// getting pointer of PTE.
	pt_entry_t* pte = &(thrid_pt[t_pd]);
	if (is_valid(pte)) hit_count++;
	else
	{
		miss_count ++;
		unsigned int nf = allocate_frame(pte);
		if (pte->offt != INVALID_SWAP)
		{
			swap_pagein(nf, pte->offt);
			// make space to record flag.
			pte->frame = nf << bit_shift;
			pte->frame &= ~mask_dirty_bit;
		}
		else	
		{
			init_frame(nf);
			pte->frame = nf << bit_shift;
			pte->frame |= mask_dirty_bit;
		}
	}

	pte->frame |= mask_valid_bit;
	ref_count ++;
	if (type == 'M' || type == 'S') pte->frame |= mask_dirty_bit;
	// revert the shift, since it has been record in pte
	return pte->frame >> bit_shift;
}

void print_pagetable(void)
{
	for (int i = 0; i < 4096; i ++){
		if (first_pd[i].next_level){
			for (int j = 0; j < 4096; j ++){
				if (first_pd[i].next_level[j].real_pt){
					for (int q = 0; q < 4096; q++)
						if (is_valid (&first_pd[i].next_level[j].real_pt[q])){
							printf("hi!");
						}else if (first_pd[i].next_level[j].real_pt[q].offt != INVALID_SWAP){
							printf("hi!");
						}
				}
			}
		}
	}

}


void free_pagetable(void)
{
	for (int i = 0; i < PAGE_SIZE; i ++)
	{
		if (first_pd[i].next_level)
		{
			for (int j = 0; j < PAGE_SIZE; j ++)
			{
				if (first_pd[i].next_level[j].real_pt)
				{
					free(first_pd[i].next_level[j].real_pt);
				}
			}
			free(first_pd[i].next_level);
		}
	}
}
