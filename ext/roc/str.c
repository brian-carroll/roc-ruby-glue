#include <ruby.h>
#include <stdbool.h>
#include "roc.h"

#define SMALL_STRING_SIZE sizeof(struct roc_big_str)
#define SMALL_STR_MAX_LENGTH (SMALL_STRING_SIZE - 1)
#define RC_WORDS 1

struct roc_big_str
{
  char *bytes;
  size_t length;
  size_t capacity;
};

union roc_str
{
  struct roc_big_str big;
  char small[SMALL_STRING_SIZE];
};

static inline bool is_small(const union roc_str *str)
{
  return str->small[SMALL_STR_MAX_LENGTH] < 0;
}

static inline size_t allocated_words(size_t chars)
{
  size_t aligned_words = (chars + sizeof(size_t) - 1) / sizeof(size_t);
  return aligned_words + RC_WORDS;
}

// Get the total memory size of the Roc Str and its contents
static size_t dsize(const void *data)
{
  const union roc_str *str = data;
  size_t byte_size = sizeof(union roc_str);
  if (!is_small(str))
  {
    byte_size += allocated_words(str->big.capacity) * sizeof(size_t);
  }
  return byte_size;
}

static void dfree(void *data)
{
  union roc_str *str = data;
  if (!is_small(str) && str->big.bytes)
  {
    ssize_t *words = (ssize_t *)str->big.bytes;
    ssize_t *rc_ptr = words - RC_WORDS;
    ssize_t encoded_rc = *rc_ptr;
    // Encoded refcount should always be negative. If it wrapped to positive, Roc must have already freed the string.
    if (encoded_rc > 0)
    {
      rb_raise(rb_eRuntimeError, "Ruby tried to free a Roc Str, but Roc has already freed it. This is a bug in the platform or glue code.");
    }
    if (encoded_rc != REFCOUNT_ONE)
    {
      rb_raise(rb_eRuntimeError, "Ruby tried to free a Roc Str while Roc still has a reference to it. This is a bug in the platform or glue code.");
    }
    ruby_xfree(rc_ptr);
  }
  ruby_xfree(data);
}

// A Ruby TypedData definition, so that Ruby's GC knows what to do with Roc Str
const rb_data_type_t roc_str_type = {
    .wrap_struct_name = "Roc::Str",
    .function = {
        .dmark = NULL, // No child Ruby objects to mark
        .dfree = dfree,
        .dsize = dsize,
    },
};

// Create a Roc Str inside a Ruby TypedData struct
static VALUE alloc_roc_str(VALUE klass)
{
  union roc_str *s;
  VALUE v = TypedData_Make_Struct(klass, union roc_str, &roc_str_type, s);
  s->small[SMALL_STR_MAX_LENGTH] = 0x80;
  return v;
}

static VALUE str_initialize(VALUE self, VALUE source_ruby_str)
{
  union roc_str *str = RTYPEDDATA_DATA(self);

  long ruby_len = RSTRING_LEN(source_ruby_str);
  char *ruby_ptr = RSTRING_PTR(source_ruby_str);

  if (ruby_len <= SMALL_STR_MAX_LENGTH)
  {
    str->small[SMALL_STR_MAX_LENGTH] = 0x80 | ruby_len;
    MEMCPY(str->small, ruby_ptr, char, ruby_len);
  }
  else
  {
    size_t alloc_size_in_words = allocated_words(ruby_len);
    size_t *words = ALLOC_N(size_t, alloc_size_in_words);
    words[0] = REFCOUNT_ONE;
    words[alloc_size_in_words - 1] = 0; // zero padding (nice for debugging)

    char *roc_chars = (char *)(&words[1]);
    MEMCPY(roc_chars, ruby_ptr, char, ruby_len);

    str->big = (struct roc_big_str){
        .bytes = roc_chars,
        .length = ruby_len,
        .capacity = (alloc_size_in_words - 1) * sizeof(size_t),
    };
  }
  return Qnil;
}

static VALUE str_to_s(VALUE self)
{
  union roc_str *str = RTYPEDDATA_DATA(self);
  return is_small(str)
             ? rb_str_new(str->small, str->small[SMALL_STR_MAX_LENGTH] & 0x7F)
             : rb_str_new(str->big.bytes, str->big.length);
}

void Init_Str(void)
{
  VALUE mRoc = rb_define_module("Roc");
  VALUE cStr = rb_define_class_under(mRoc, "Str", rb_cObject);
  rb_define_alloc_func(cStr, alloc_roc_str);
  rb_define_method(cStr, "initialize", str_initialize, 1);
  rb_define_method(cStr, "to_s", str_to_s, 0);
}
