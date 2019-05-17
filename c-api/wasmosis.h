//
// Very provisional C header for Wasmosis syscall API.
//
// Still being worked out in details.
//

#ifndef __WASMOSIS_H
#define __WASMOSIS_H

#include <stdlib.h>
#include <stdbool.h>


//
// Object handles and other capabilities are represented as local
// indexes to externally stored JS objects.
//
// You can only forge references to objects attached to the local
// module, which means you either created them locally or they
// were sent in from another module.
//
typedef size_t cap_t;

#define CAP_NULL ((cap_t)0)

//
// Retain another reference to this capability.
//
// Any translated caps on an incoming RPC call will be released
// after return, so we must retain them to copy them to a permanent
// index in the caps namespace.
//
// In contrast, return values are always owned by the caller.
//
extern cap_t __wasmosis_cap_retain(
    cap_t cap
);
#define cap_retain __wasmosis_cap_retain

//
// Revoke an owned cap, so any further attempt to use it will fail.
// Won't work on caps belonging to another module.
//
extern void __wasmosis_cap_revoke(
    cap_t cap
);
#define cap_revoke __wasmosis_cap_revoke

//
// Release the given cap reference from the current module.
// Note the same referenced object may exist in multiple slots.
// Slots may be reused once freed.
//
// Not guaranteed to free resources used.
//
extern void __wasmosis_cap_release(
    cap_t cap
);
#define cap_release __wasmosis_cap_release

//
// Create a writable, revocable view of a buffer in this module's memory.
//
extern cap_t __wasmosis_recvbuf_create(
    void* dest,
    size_t len
);
#define recvbuf_create __wasmosis_recvbuf_create

//
// Write data from this module's memory into the remote module's memory.
// Cannot exceed the given buffer range, or write after the cap was revoked.
// Returns number of bytes copied.
//
extern size_t __wasmosis_recvbuf_write(
    cap_t buf,
    const void* src,
    size_t len
);
#define recvbuf_write __wasmosis_recvbuf_write


//
// Create a readable, revocable view of a buffer in this module's memory.
//
extern cap_t __wasmosis_sendbuf_create(
    const void* dest,
    size_t len
);
#define sendbuf_create __wasmosis_sendbuf_create

//
// Read data from the remote module's memory into this module's memory.
// Cannot exceed the given buffer range, or read after the cap was revoked.
// Returns number of bytes copied.
//
extern size_t __wasmosis_sendbuf_read(
    cap_t buf,
    void* dest,
    size_t len
);
#define sendbuf_read __wasmosis_sendbuf_read


//
// Boxed numerics take fewer resources to transfer than
// send and receive buffers, though for several of them it
// could be cheaper to use a struct.
//
extern cap_t __wasmosis_box_i32(int32_t val);
extern cap_t __wasmosis_box_u32(uint32_t val);
extern cap_t __wasmosis_box_f32(float val);
extern cap_t __wasmosis_box_f64(double val);
extern cap_t __wasmosis_box_bool(bool val);

#define box_i32 __wasmosis_box_i32
#define box_u32 __wasmosis_box_u32
#define box_f32 __wasmosis_box_f32
#define box_f64 __wasmosis_box_f64
#define box_bool __wasmosis_box_bool

extern int32_t  __wasmosis_unbox_i32(cap_t box);
extern uint32_t __wasmosis_unbox_u32(cap_t box);
extern float    __wasmosis_unbox_f32(cap_t box);
extern double   __wasmosis_unbox_f64(cap_t box);
extern bool     __wasmosis_unbox_bool(cap_t box);

#define unbox_i32 __wasmosis_unbox_i32
#define unbox_u32 __wasmosis_unbox_u32
#define unbox_f32 __wasmosis_unbox_f32
#define unbox_f64 __wasmosis_unbox_f64
#define unbox_bool __wasmosis_unbox_bool


//
// Handle message callbacks have caps translation baked in; we
// receive a list of borrowed caps and optionally return one back.
// Scalar arguments and return values can be sent through boxes,
// which do not require heap allocation and should be cheaper to
// transfer than a small buffer.
//
// The handle is passed in so you can get its user_data for
// routing callbacks or object methods, or revoke it for strict
// single-use callbacks.
//
// If no cap return is required, return CAP_NULL.
//
typedef cap_t (*handle_callback0_t)(cap_t handle, int index);
typedef cap_t (*handle_callback1_t)(cap_t handle, int index, cap_t arg1);
typedef cap_t (*handle_callback2_t)(cap_t handle, int index, cap_t arg1, cap_t arg2);
typedef cap_t (*handle_callback3_t)(cap_t handle, int index, cap_t arg1, cap_t arg2, cap_t arg3);
typedef cap_t (*handle_callback4_t)(cap_t handle, int index, cap_t arg1, cap_t arg2, cap_t arg3, cap_t arg4);


//
// Create a generic handle cap which can be sent away to other
// modules as an unforgeable object reference.
//
// The class ref is a fixed pointer value used to distinguish
// between different handle types. If you need something more
// like inheritence-based 'instanceof', you can roll that with
// your own class structs.
//
// The user_data value can be looked up on the cap if we receive
// it back from another module later, so we can store something
// handy like a pointer to an internal state object.
//
// A handle may also hold 0 or more function references, which
// may be called with kernel-mediated transfer of cap arguments
// across modules. This is suitable for modeling opaque handles
// (with no funcs), closures (one func), or OO objects (multiple
// funcs in an application-protocol-defined interface).
//
extern cap_t __wasmosis_handle_create(
    void* class_ref,
    void* user_data,
    void* funcs_start,
    size_t funcs_len
);
#define handle_create __wasmosis_handle_create

//
// If the cap was created with handle_create in this module with
// the given class_ref value, then the internal user_data value is
// returned else NULL.
//
extern void* __wasmosis_handle_user_data(
    cap_t handle,
    void* class_ref
);
#define handle_user_data __wasmosis_handle_user_data

//
// Make a synchronous message call to a local or remote handle.
// The kernel will translate the caps arguments and return value
// for cross-module calls.
//
// Args are borrowed, and should be retained in the callee if you
// need to keep them. Return values are owned by the caller.
// Beware that even if you're expecting to get CAP_NULL back,
// you need to release just in case something was transferred
// that you didn't expect.
//
extern cap_t __wasmosis_handle_call0(
    cap_t port,
    size_t index
);
#define handle_call0 __wasmosis_handle_call0

extern cap_t __wasmosis_handle_call1(
    cap_t port,
    cap_t arg1
);
#define handle_call1 __wasmosis_handle_call1

extern cap_t __wasmosis_handle_call2(
    cap_t port,
    cap_t arg1,
    cap_t arg2
);
#define handle_call2 __wasmosis_handle_call2

extern cap_t __wasmosis_handle_call3(
    cap_t port,
    cap_t arg1,
    cap_t arg2,
    cap_t arg3
);
#define handle_call3 __wasmosis_handle_call3

extern cap_t __wasmosis_handle_call4(
    cap_t port,
    cap_t arg1,
    cap_t arg2,
    cap_t arg3,
    cap_t arg4
);
#define handle_call4 __wasmosis_handle_call4

#endif
