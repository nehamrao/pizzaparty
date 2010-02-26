#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include <stdint.h>
#include "frame.h"

bool swap_init (void);
bool swap_in (struct frame_struct *pframe);
bool swap_out (struct frame_struct *pframe);
void swap_free (uint32_t * pte);

#endif /* vm/swap.h */
