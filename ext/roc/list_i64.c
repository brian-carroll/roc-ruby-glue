#include <ruby.h>
#include <stdbool.h>
#include "alloc.h"

//
// Define the element type T
//

#define T int64_t
#define RUBY_CLASSNAME ListI64

T elem_ruby_to_roc(VALUE v)
{
  return NUM2INT(v);
}

VALUE elem_roc_to_ruby(T v)
{
  return INT2NUM(v);
}

void (*free_elem)(T) = NULL;

// End of element-specific definitions

#define STRINGIFY(x) #x
#define CONCAT(x, y) x##y
#define INIT_FN(x) CONCAT(Init_, x)

static const size_t ALIGNMENT =
    (__alignof__(size_t) > __alignof__(T))
        ? __alignof__(size_t)
        : __alignof__(T);

struct roc_list
{
  T *elements;
  size_t length;
  size_t capacity;
};

// Get the total memory size of the List and its contents
static size_t dsize(const void *data)
{
  const struct roc_list *list = data;
  return sizeof(struct roc_list)        // struct
         + (list->capacity * sizeof(T)) // elements
         + ALIGNMENT;                   // refcount
}

static void dfree(void *data)
{
  const struct roc_list *list = data;
  if (list->elements)
  {
    if (free_elem)
    {
      for (size_t i = 0; i < list->length; i++)
      {
        free_elem(list->elements[i]);
      }
    }
    void* allocation = (void*)list->elements - ALIGNMENT;
    roc_dealloc(allocation, ALIGNMENT);
  }
  ruby_xfree(data);
}

// A Ruby TypedData definition, so that Ruby's GC knows what to do with Roc data structures
static const rb_data_type_t roc_list_type = {
    .wrap_struct_name = "Roc::" STRINGIFY(RUBY_CLASSNAME),
    .function = {
        .dmark = NULL, // No child Ruby VALUEs. Roc structures only contain Roc values.
        .dfree = dfree,
        .dsize = dsize,
    },
};

// Create a Roc Str inside a Ruby TypedData struct
static VALUE list_allocate(VALUE klass)
{
  struct roc_list *list;
  return TypedData_Make_Struct(klass, struct roc_list, &roc_list_type, list);
}

static VALUE list_initialize(VALUE self, VALUE ruby_array_int)
{
  size_t len = rb_array_len(ruby_array_int);
  if (!len)
  {
    return Qnil;
  }

  struct roc_list *list = RTYPEDDATA_DATA(self);

  size_t elems_size_bytes = len * sizeof(T);
  size_t rc_space_bytes = ALIGNMENT;
  size_t rc_offset = rc_space_bytes - sizeof(size_t);

  void *allocation = roc_alloc(elems_size_bytes + rc_space_bytes, ALIGNMENT);
  T *elements = (T *)(allocation + rc_space_bytes);
  ssize_t *rc_ptr = (ssize_t *)(allocation + rc_offset);
  *rc_ptr = REFCOUNT_ONE;

  *list = (struct roc_list){
      .elements = elements,
      .length = len,
      .capacity = len,
  };

  for (size_t i = 0; i < len; i++)
  {
    VALUE ruby_int = rb_ary_entry(ruby_array_int, i);
    list->elements[i] = elem_ruby_to_roc(ruby_int);
  }

  return Qnil;
}

static VALUE list_to_a(VALUE self)
{
  struct roc_list *list = RTYPEDDATA_DATA(self);
  VALUE ruby_array = rb_ary_new_capa(list->length);

  for (size_t i = 0; i < list->length; i++)
  {
    rb_ary_push(ruby_array, elem_roc_to_ruby(list->elements[i]));
  }

  return ruby_array;
}

// This function name expands to something like Init_ListI64, Init_ListStr, etc.
void INIT_FN(RUBY_CLASSNAME)(void)
{
  VALUE mRoc = rb_define_module("Roc");
  VALUE cList = rb_define_class_under(mRoc, STRINGIFY(RUBY_CLASSNAME), rb_cObject);
  rb_define_alloc_func(cList, list_allocate);
  rb_define_method(cList, "initialize", list_initialize, 1);
  rb_define_method(cList, "to_a", list_to_a, 0);
}
