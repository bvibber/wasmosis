# Wasmosis kernel

_This is currently a rough design document, the code is in progress and will be added shortly. -brion_

__Wasmosis__ is (will be) a "microkernel-inspired" JavaScript supervisor for WebAssembly modules to securely interoperate without sharing a memory address space. It's intended to be suitable for running semi-trusted or untrusted plugins in a JS or WebAssembly application.

# Background

The name "Wasmosis" comes from "Wasm", short for "WebAssembly", and "osmosis", a biochemical process where ions pass across a semi-permeable membrane in a specific direction. This is meant to invoke the secure passing of certain data only across the module/kernel boundary.

L4 and seL4 microkernels inspired the capability-based security and the message-passing via synchronous function call, with modifications for a single-threaded web context.

## Goals

High-level:
* Provide a way for web apps to embed WebAssembly "plugins", "widgets", etc with relatively high security guarantees.
* Have this model work for both trusted JS-implemented modules and untrusted Wasm-implemented modules.
* Allow safe transfer of object references from module to module (eg, modules can safely delegate their permissions on to their own plugins)
* Implement an example app+plugin in the web/browser environment.

Low-level:
* Define a minimal "microkernel"-like API+ABI for capability-passing and synchronous calls between modules with separate address spaces.
* Define mid-level web-oriented embedding APIs providing an asynchronous event loop, event subscription, and promises.
* Implement a JS kernel.
* Implement a C API header.
* Implement a Rust API crate.

Example plans to exercise the kernel API and implementation:
* Implement a sample JS module exposing an API (say, an HTML `<canvas>` drawing context with mouse/touch events).
* Implement a sample Wasm widget module (say, a simple paint app).
* Implement a sample Wasm plugin module for it (say, an image filter).

# Process model

WebAssembly module instances each have a 4 GiB _linear memory_ address space, with usable memory in a single flat run from position 0 until the current size of memory. Memory can be expanded in 64 KiB increments up to a limit predefined when creating the memory. There is also a _table_ of runtime-callable function references, with C function pointers implemented as table indexes.

In traditional dynamic linking, the memory and table are shared between instances of different modules. This allows any module to modify all the memory and call all the functions of its fellow linkees -- not a desireable property for untrusted plugins.

Wasmosis instead lets each module instance have its own memory and function table address spaces, plus a kernel-maintained capabilities namespace. Function calls between modules are mediated through the kernel, which translates the arguments from one module's caps namespace to the other. The cap values in the target namespace can then be accessed via kernel syscalls, or passed on to another module.

## Single vs multi-threading

The JavaScript / web platform is primarily a single-threaded world today, and Wasmosis targets that. This model can be extended to run separate contexts in Web Workers, with async message passing between contexts.

True multithreading with SharedArrayBuffer would allow synchronous calls across workers, and might be considered in the future.

# Security model

* Memory safety:
    * (good) Separate address spaces, so buffer transfers are through cap transfer and syscalls.
    * (good) Transfer buffers have owner-controlled lifetimes, so cannot be used after invalidation.
    * (good) Transfer buffers are either read-only or write-only, to avoid exposing uninitialized memory.
* DoS issues:
    * (good) Linear memory usage can be bounded by the kernel at module instantiation time.
    * (good) Number and type of caps created by a process can be tracked by the kernel, so reasonable limits can be applied.
    * (slightly bad) Deep function call stack recursion will throw a trap, which can be caught by the kernel and the module terminated safely. However if another module gets very close to the stack limit and then calls into your module, you might be the one that gets terminated.
    * (somewhat bad) A call to a port may never yield control back (the halting problem). If running on the main web thread, this will eventually cause a timeout and prompt the user to halt it, which may leave module data in an inconsistent state due to failure to unwind the stack. If running in a web worker, it will keep running until the worker is terminated.

For web applications, this mostly means that your failure scenario is a "crash" of the web app, fixed by a page refresh or "rebooting" just your affected WebAssembly modules. However if you "autorun" anything, you need to be able to run enough of the app to disable the offending plugin or your users could end up stuck in a broken state.

# Kernel object types and syscalls

Capability references may point to one of various types of kernel objects. This is a single shared namespace per module; making a syscall on an object of the wrong type will fail gracefully, but making message calls to a handle with an unexpected interface may be less forgiving! Processes are assumed to be able to keep track of their own indexes, just like they track pointer types. :)

* Null sigil (`CAP_NULL = 0`, cannot be released or reassigned)
* Boxed values
* Send buffers
* Recv buffers
* Handles

Objects keep track of which module created them and the owner may perform privileged operations on them, such as revoking the validity of a buffer or retrieving the local pointer value of a handle. Attempting to perform these operations on another module's handle will fail gracefully.

## Generic syscalls

The following syscall functions for the module->kernel API are valid on all cap types:

* `cap_release(cap)`: Releases the cap index from the local namespace, and queues it for reuse on future cap allocation. Call this on caps you got as a return value from a syscall or message call, but never on "borrowed" references from port call arguments. This does not free any resources belonging to the owner process, which may have other live references.
* `cap_retain(cap) -> cap2`: Creates a new index for the cap reference, which you may keep and use after the original one is `cap_release`d.
* `cap_revoke(cap)`: Invalidates an owned object, so it can no longer be used. Suitable for calling to revoke buffers and handles from being used after resources are freed. Note that this does not release the index, which is still allocated.

## Boxed values

To simplify the calling conversion functions, all port call arguments are transferred through capabilities, including numeric primitives. In the JS kernel this stores a number or boolean primitive in the same array slot that would have been an object reference for other types, so this should be pretty efficient -- no actual heap allocation is required for boxing.

* `box_i32(val) -> cap`, `unbox_i32(cap) -> val`
* `box_u32(val) -> cap`, `unbox_u32(cap) -> val`
* `box_f32(val) -> cap`, `unbox_f32(cap) -> val`
* `box_f64(val) -> cap`, `unbox_f64(cap) -> val`
* `box_bool(val) -> cap`, `unbox_bool(cap) -> val`

This also means that boxes are "unowned", since they don't have space for a reference. You cannot revoke a boxed value cap. Unboxing a different numeric type from that used to create will convert; unboxing a cap of another type will return 0 (or false).

Note that there is no `i64` or `u64` boxed type, as until `BigInt` is reliably available WebAssembly and JS can't send `i64`s to each other.

Similarly, floating-point transfers will canonicalize NaN values due to the limitations of the JavaScript kernel. If you need to transfer 64-bit ints or custom NaN values across modules, use a byte-wise buffer.

## Send buffers

Send buffers are sent to another module, which can read them but not write back to them or access beyond the boundaries. They're suitable for constant data that must not be altered.

* `sendbuf_create(src_ptr, len) -> cap`
* `sendbuf_read(cap, dest_ptr, len) -> bytes_read`: Copies the buffer from one module's memory to another, within defined limits. This advances an internal pointer, and may be called multiple times with different sub-destinations.
* `sendbuf_bytes_read(cap) -> bytes_read`: For an owned cap, returns the internal pointer of how many bytes have been read.

When done with it, use `cap_revoke` to invalidate the pointer.

## Recv buffers

Recv buffers can be written to by other modules, but they can't read what's in it. This allows sending a reference to allocated-but-uninitialized memory without fear of old data being read out of it.

* `recvbuf_create(dest_ptr, len) -> cap`
* `recvbuf_write(cap, src_ptr, len) -> bytes_written`: Copies the buffer from one module's memory to another, within defined limits. This advances an internal pointer, and may be called multiple times with different sub-sources.
* `sendbuf_bytes_written(cap) -> bytes_written`: For an owned cap, returns the internal pointer of how many bytes have been written. This should be used instead of trusting a count returned by another module, to avoid consuming your own uninitialized memory.

When done with it, use `cap_revoke` to invalidate the pointer.

## Handles

A handle holds an opaque pointer in your module's address space, which can be retrieved if you're the owner but not otherwise. This lets modules create un-forgeable references to objects or structs, which can be passed away and then passed back.

Handles may also hold zero or more function pointers, which holders of the handle may invoke as "message" calls. This makes handles suitable for opaque handles (state pointer only), closures (state pointer plus one function pointer), or object interfaces (state pointer plus multiple function pointers, indexed by application-specific convention).

* `handle_create(class_ref, user_data, funcs_ptr, funcs_len) -> cap`: create a new handle with the give opaque class ref pointer, instance pointer, and an array of zero or more function pointers.
* `handle_user_data(cap, class_ref) -> user_data`: retrieve the instance pointer for an owned cap, given a matching opaque class ref pointer.

The "class ref" opaque pointer is used to validate the handle along with the cap ownership -- it's convenient to use something like a symbol address that's type-unique within the process. If you ask for a handle's user_data without owning it, or without a matching class_ref, you'll get NULL back.

When done with it, use `cap_revoke` to invalidate the pointer.

## Message calls

Given a handle and a method index, you can make synchronous calls to it with some specific number of parameters (currently from 0 to 4) and a single return value, all of which are in the caps namespace.

When you create a handle, you associate it with an array of callable function pointers and an opaque user data pointer, which can be retrieved by the owning module when taking an incoming call. This allows routing runtime-generated callbacks as well as traditional fixed functions.

Calling a port with the wrong number of parameters is not yet fully defined behavior, it will probably work but pass null caps for missing params.

* `handle_call0(handle, index) -> cap_ret`
* `handle_call1(handle, index, cap1) -> cap_ret`
* `handle_call2(handle, index, cap1, cap2) -> cap_ret`
* `handle_call3(handle, index, cap1, cap2, cap3) -> cap_ret`
* `handle_call4(handle, index, cap1, cap2, cap3, cap4) -> cap_ret`
* ... more may be added, perhaps up to 8.

Note that caps passed as arguments are "borrowed" -- on cross-module calls the kernel layer will retain/release the transferred caps on entry/exit. So if you receive a cap as an argument and want to keep it for later, you must `cap_retain` it and keep the copy.

The cap returned as a return value is "caller-owned" so you must call `cap_release` when you're done with it -- even if it's a boxed integer, you need to release the index so it can be reused.

# ABI

The ABI is meant to be as minimal as possible, making few assumptions about the module's memory layout or imports/exports. For instance, the kernel never allocates data on the module's stack or heap, nor alters its function tables. This allows modules to be C-style with a global stack pointer and internal `malloc`/`free`... or have wildly custom memory layouts.

Syscall functions will be provided as imports to the WebAssembly module instantiation, in a two-level namespace where the first namespace is `__wasmosis` and the second is eg `cap_release`.

At least one entry point function should be provided as an export. Need to consider whether this will be application-protocol-defined or a standard initialization time function created...

* __@todo__ work that out

# API

In the C API, syscall names will be exposed in a flat namespace as `__wasmosis_cap_release` etc, probably `#define`'d back into short names. (?)

A Rust API building on the same syscalls will have prettier structs and functions wrapping these to aid in type-safety and lifetime control.

# Portability

Non-JS kernels for non-web WebAssembly embeddings would also be possible, but I have not yet looked into it.

Further, there is nothing really WebAssembly-specific in the API... The C and Rust APIs, if defined with appropriately portable types, could also work when compiling to native code.

A native implementation of the kernel and ABI that uses container isolation is left as an exercise for another time... however it may be useful for testing to make native builds of things that use an in-process dummy kernel. This would provide no memory isolation, but could keep separate caps namespaces for each module, switching active processes at the `handle_call*` boundary.

# License

MIT-style license for the initial JS kernel implementation and C and Rust APIs.
