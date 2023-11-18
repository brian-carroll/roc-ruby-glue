#ifndef ROC_H
#define ROC_H

#include <ruby.h>

#define REFCOUNT_ONE ((ssize_t)1 << (sizeof(ssize_t) * 8 - 1))

void *roc_alloc(size_t size, uint32_t alignment);
void *roc_realloc(void *ptr, size_t new_size, size_t old_size, size_t alignment);
void roc_dealloc(void *ptr, uint32_t alignment);
void roc_panic(const char *message, uint32_t _tag_id);
void *roc_memset(void *ptr, int value, size_t num_bytes);

struct roc_type
{
  char *name;
  size_t alignment;
  size_t stack_size;
  void (*from_ruby)(struct roc_type*, void *, VALUE);
  void (*free)(struct roc_type*, void *);
  VALUE (*to_ruby)(struct roc_type*, void *);
  size_t n_type_vars;
  struct roc_type type_vars[];
};

extern VALUE cRocValue;

#endif
