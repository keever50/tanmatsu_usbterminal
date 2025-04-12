#include "ezcmd2.h"
#include <string.h>

/******************************************************************************
 * Private Functions
 *****************************************************************************/

int ezcmd_remove_char(struct ezcmd_inst_s *inst)
{
  /* Boundry */

  if (inst->cursor_pos == 0)
  {
    return -1;
  }
  
  /* Erase previous */

  inst->buffer[--inst->cursor_pos] = '\0';

  return 0;
}

int ezcmd_add_char(struct ezcmd_inst_s *inst, char c)
{
  /* Check for overflow. Leave space for null terminator */

  if (inst->cursor_pos >= inst->buffersize - 1)
  {
    return -1;
  }

  /* Append to buffer */

  inst->buffer[inst->cursor_pos++] = c;

  return 0;
}

void ezcmd_whitelines_to_nulls(struct ezcmd_inst_s *inst)
{
  // TODO quotes disable conversion
  for (size_t i = 0; i < inst->buffersize; i++)
  {
    char c = inst->buffer[i];
    if (c == ' ')
    {
      inst->buffer[i] = '\0';
    }
  }
}

/******************************************************************************
 * Public Functions
 *****************************************************************************/

void ezcmd_init(struct ezcmd_inst_s *inst, char *userbuffer, size_t buffersize)
{
  inst->buffer = userbuffer;
  inst->buffersize = buffersize;
  ezcmd_reset(inst);
}

int ezcmd_put(struct ezcmd_inst_s *inst, char c)
{
  /* Dont add keys when "enter" been pressed before */

  if (inst->command_ready)
  {
    return 1;
  }

  /* Check for special keys */

  switch (c)
  {
    /* Return */

    case '\n':
    case '\r':
    {
      inst->command_ready = 1;
      ezcmd_whitelines_to_nulls(inst);
      return 1;
    }

    case '\b':
    {
      return ezcmd_remove_char(inst);
    }

    default:
    {
      return ezcmd_add_char(inst, c);
    }
  }

  return 0;
}

void ezcmd_reset(struct ezcmd_inst_s* inst)
{
  inst->cursor_pos = 0;
  inst->command_ready = 0;
  inst->iterator_pos = 0;
  inst->first_iteration = 1;

  memset(inst->buffer, 0, inst->buffersize);
}

char* ezcmd_iterate_arguments(struct ezcmd_inst_s *inst)
{
  /* The first iteration should return the first argument */

  if (inst->first_iteration)
  {
    inst->first_iteration = 0;
    return inst->buffer;
  }

  /* Find next argument */

  for (; ; )
  {
    char c = inst->buffer[inst->iterator_pos++];

    /* Boundry. No more arguments. Return NULL */

    if (inst->iterator_pos >= inst->cursor_pos)
    {
      return NULL;
    }

    /* Arguments start after nulls*/

    if (c == '\0')
    {
      /* We already incremented our position before,
       * so this is the first character of the argument
       */

      return inst->buffer + inst->iterator_pos;
    }
  }

  return NULL;
}