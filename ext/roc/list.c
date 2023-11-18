#include <ruby.h>
#include <stdbool.h>
#include "roc.h"

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

// The underlying Roc List
struct roc_list
{
  void *elements;
  size_t length;
  size_t capacity;
};

// The list and its metadata
struct roc_list_meta
{
  struct roc_list list;
  struct roc_type elem_type;
};

ID id_roc_type;

static inline size_t adjusted_alignment(size_t elem_alignment)
{
  if (elem_alignment < __alignof__(size_t))
  {
    elem_alignment = __alignof__(size_t);
  }
  return elem_alignment;
}

// Get the total memory size of the List and its contents
static size_t dsize(const void *data)
{
  struct roc_list_meta *meta = data;
  return sizeof(struct roc_list_meta)                         // struct
         + (meta->list.capacity * meta->elem_type.stack_size) // elements
         + adjusted_alignment(meta->elem_type.alignment);     // refcount
}

static void dfree(void *data)
{
  const struct roc_list_meta *meta = data;
  const struct roc_list list = meta->list;
  const struct roc_type elem_type = meta->elem_type;
  if (list.elements)
  {
    if (free_elem)
    {
      void *end = list.elements + (list.length * elem_type.stack_size);
      for (void *elem = list.elements; elem < end; elem += elem_type.stack_size)
      {
        elem_type.free(elem);
      }
    }
    size_t elem_alignment = adjusted_alignment(elem_type.alignment);
    void *allocation = (void *)list.elements - elem_alignment;
    roc_dealloc(allocation, elem_alignment);
  }
  ruby_xfree(data);
}

// A Ruby TypedData definition, so that Ruby's GC knows what to do with Roc data structures
static const rb_data_type_t list_type_ruby = {
    .wrap_struct_name = "Roc::List",
    .function = {
        .dfree = dfree,
        .dsize = dsize,
    },
};

// Create a Roc Str inside a Ruby TypedData struct
static VALUE list_allocate(VALUE klass)
{
  struct roc_list_meta *list;
  return TypedData_Make_Struct(klass, struct roc_list_meta, &list_type_ruby, list);
}

static VALUE initialize(VALUE self, VALUE elem_class_v, VALUE ruby_array)
{
  // Retrieve the metadata struct from the newly allocated Ruby TypedData value
  struct roc_list_meta *meta = RTYPEDDATA_DATA(self);

  // Retrieve the element Roc type from the Ruby class
  struct RClass *elem_class = (struct RClass *)elem_class_v;
  VALUE elem_type_v = rb_funcall(elem_class_v, id_roc_type, 0);
  struct roc_type *elem_type = (struct roc_type *)elem_type_v;

  size_t len = rb_array_len(ruby_array);
  void *elements = NULL;
  if (len > 0)
  {
    size_t elems_size_bytes = len * elem_type->stack_size;
    size_t elems_alignment = adjusted_alignment(elem_type->alignment);
    size_t rc_offset = elems_alignment - sizeof(size_t);
    void *allocation = roc_alloc(elems_size_bytes + elems_alignment, elems_alignment);
    elements = allocation + elems_alignment;
    ssize_t *rc_ptr = (ssize_t *)(allocation + rc_offset);
    *rc_ptr = REFCOUNT_ONE;
  }

  *meta = (struct roc_list_meta){
      .list = (struct roc_list){
          .elements = elements,
          .length = len,
          .capacity = len,
      },
      .elem_type = *elem_type,
  };

  void *roc_elem = meta->list.elements;
  for (size_t i = 0; i < len; i++)
  {
    VALUE ruby_elem = rb_ary_entry(ruby_array, i);
    meta->elem_type.from_ruby(roc_elem, ruby_elem);
    roc_elem += meta->elem_type.stack_size;
  }

  return Qnil;
}

static VALUE m_from_ruby(VALUE klass, VALUE elem_class, VALUE ruby_array)
{
  VALUE new_obj = list_allocate(klass);
  initialize(new_obj, elem_class, ruby_array);
}

static VALUE to_ruby(struct roc_list_meta *meta)
{
  VALUE ruby_array = rb_ary_new_capa(meta->list.length);

  void *roc_elem = meta->list.elements;
  for (size_t i = 0; i < meta->list.length; i++)
  {
    VALUE ruby_elem = meta->elem_type.to_ruby(roc_elem);
    rb_ary_push(ruby_array, ruby_elem);
  }

  return ruby_array;
}

static VALUE m_to_ruby(VALUE self)
{
  struct roc_list_meta *meta = RTYPEDDATA_DATA(self);
  return to_ruby(meta);
}

static VALUE from_roc_ptr(VALUE klass, VALUE roc_ptr)
{
  VALUE new_obj = list_allocate(klass);
  struct roc_list_meta *meta = RTYPEDDATA_DATA(new_obj);
  meta->list = *(struct roc_list *)roc_ptr;
  return new_obj;
}

static VALUE write_roc_ptr(VALUE self, VALUE roc_ptr)
{
  struct roc_list_meta *meta = RTYPEDDATA_DATA(self);
  *(struct roc_list *)roc_ptr = meta->list;
  return Qnil;
}

static VALUE list_free(VALUE self)
{
  struct roc_list_meta *meta = RTYPEDDATA_DATA(self);
  dfree(meta);
  return Qnil;
}

const struct roc_type list_type_roc = {
    .name = "List",
    .alignment = __alignof__(struct roc_list),
    .stack_size = sizeof(struct roc_list),
    .to_ruby = to_ruby,
    .from_ruby =,
    .free = dfree,
};

static VALUE m_roc_type(VALUE _self)
{
  return (VALUE)(void *)&list_type_roc;
}

void Init_List(void)
{
  VALUE mRoc = rb_define_module("Roc");
  VALUE cList = rb_define_class_under(mRoc, STRINGIFY(RUBY_CLASSNAME), rb_cObject);
  rb_define_alloc_func(cList, list_allocate);

  rb_define_singleton_method(cList, "from_ruby", m_from_ruby, 2);

  rb_define_method(cList, "initialize", initialize, 2);
  rb_define_method(cList, "to_ruby", m_to_ruby, 0);
  rb_define_method(cList, "write_roc_ptr", write_roc_ptr, 1);
  rb_define_method(cList, "free", list_free, 0);

  ID id_roc_type = rb_intern("roc_type");
}
