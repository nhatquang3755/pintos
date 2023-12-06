#include "vm/swap.h"
#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include "vm/frame.h"
#include "vm/page.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* The swap device (Array of swap blocks). */
static struct block *swap_device;

/* Used swap pages. */
static struct bitmap *swap_bitmap;

/* Protects swap_bitmap. */
static struct lock swap_lock;

/* Number of sectors per page. */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/* Sets up swap. */
void swap_init (void) {
  // get block with role BLOCK_SWAP
  swap_device = block_get_role (BLOCK_SWAP);

  // if blocks was not given, no swap-partition will be
  // bitmap size = 0
  if (swap_device == NULL) {
    printf ("no swap device--swap disabled\n");
    swap_bitmap = bitmap_create (0);
  } else 
  /* 
     block was given
     1 bit = 1 sector => bitmap size = numbers of sector in block
  */ 
  {
    swap_bitmap = bitmap_create (block_size (swap_device) / SECTORS_PER_PAGE);
  }

  if (swap_bitmap == NULL) {
    PANIC ("couldn't create swap bitmap");
  }
  lock_init (&swap_lock);
}

/* Swaps in page P, which must have a locked frame
   (and be swapped out). */
void swap_in (struct page *p) {
  size_t i;

  // check frame
  ASSERT (p->frame != NULL);
  // check hold frame lock
  ASSERT (lock_held_by_current_thread (&p->frame->lock));
  // check swapped out
  ASSERT (p->sector != (block_sector_t) -1);

  // read each page sector from swap_device
  for (i = 0; i < SECTORS_PER_PAGE; i++) {
    block_read (swap_device, p->sector + i, p->frame->base + i * BLOCK_SECTOR_SIZE);
  }

  // after read, reset bit to be ready
  bitmap_reset (swap_bitmap, p->sector / SECTORS_PER_PAGE);
  // page sector index = -1 (no swap out)
  p->sector = (block_sector_t) -1;
}

/* Swaps out page P, which must have a locked frame. */
bool swap_out (struct page *p) {
  size_t idx;
  size_t i;

  // check frame
  ASSERT (p->frame != NULL);
  // check hold frame lock
  ASSERT (lock_held_by_current_thread (&p->frame->lock));

  // find the 1 bit that are false
  // flip the swap bit to true (being swapped out)
  lock_acquire (&swap_lock);
  idx = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
  lock_release (&swap_lock);
  if (idx == BITMAP_ERROR) {
    return false;
  }

  p->sector = idx * SECTORS_PER_PAGE;

  /*  Write out page sectors for each modified block. */
  for (i = 0; i < SECTORS_PER_PAGE; i++) {
    block_write (swap_device, p->sector + i, (uint8_t *) p->frame->base + i * BLOCK_SECTOR_SIZE);
  }

  p->private = false;
  p->file = NULL;
  p->file_offset = 0;
  p->file_bytes = 0;

  return true;
}
