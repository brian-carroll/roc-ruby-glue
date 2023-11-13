#include <ruby.h>
#include "alloc.h"

void* roc_alloc(size_t size, uint32_t _alignment)
{
  void* ptr = ruby_xmalloc(size);
  if (!ptr)
    rb_raise(rb_eNoMemError, "Roc failed to allocate memory.");
  return ptr;
}

void* roc_realloc(void* ptr, size_t new_size, size_t _old_size, size_t _alignment)
{
  void* new_ptr = ruby_xrealloc(ptr, new_size);
  if (!new_ptr)
    rb_raise(rb_eNoMemError, "Roc failed to allocate memory.");
  return new_ptr;
}

void roc_dealloc(void* ptr, uint32_t _alignment)
{
  ruby_xfree(ptr);
}

void roc_panic(const char* message, uint32_t _tag_id)
{
  rb_raise(rb_eRuntimeError, "%s", message);
}

void* roc_memset(void* ptr, int value, size_t num_bytes)
{
  return memset(ptr, value, num_bytes);
}
