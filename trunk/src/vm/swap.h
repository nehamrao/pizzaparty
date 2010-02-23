#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include <stdint.h>
#include "frame.h"

void swap_init (void);
bool swap_in (struct frame_struct *pframe);
bool swap_out (struct frame_struct *pframe);

#endif /* vm/swap.h */
