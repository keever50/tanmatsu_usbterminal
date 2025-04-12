#ifndef _EZCMD_H
#define _EZCMD_H

#include <stdio.h>
#include <stdint.h>

struct ezcmd_inst_s
{
  char *buffer;
  size_t buffersize;
  size_t cursor_pos;
  uint8_t command_ready;
  size_t iterator_pos;
  uint8_t first_iteration;
};

void ezcmd_init(struct ezcmd_inst_s *inst, char *userbuffer, size_t buffersize);
void ezcmd_reset(struct ezcmd_inst_s* inst);
int ezcmd_put(struct ezcmd_inst_s *inst, char c);
char* ezcmd_iterate_arguments(struct ezcmd_inst_s *inst);

#endif /* _EZCMD2_H */