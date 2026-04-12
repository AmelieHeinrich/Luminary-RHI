# Plan: Command Submission Model — Fence & Command Queue

## Context

The RHI currently has `LRHICommandQueue` and `LRHIFence` declared as opaque types with no implementation. This plan adds a timeline-style fence API and a command queue API that work across all backends, then implements them for Metal 3 and Metal 4.

Backend mapping:
- **D3D12** → `ID3D12Fence` (stub only, not implemented here)
- **Vulkan** → timeline `VkSemaphore` (stub only, not implemented here)
- **Metal 3** → CPU-side: `_Atomic uint64_t` + `pthread_cond_t`; GPU signal: empty `MTLCommandBuffer` + `addCompletedHandler`; GPU wait: CPU-block
- **Metal 4** → `id<MTLSharedEvent>` + `MTL4CommandQueue.signalEvent/waitForEvent`

---

## Files to Modify

| File | Change |
|------|--------|
| `src/luminary_rhi.h` | Add public `lrhi_create/destroy_command_queue`, `lrhi_command_queue_signal/wait`, `lrhi_create/destroy_fence`, `lrhi_fence_get_value/signal/wait` |
| `src/luminary_rhi_internal.h` | Add `create_command_queue`/`create_fence` to `LRHIDeviceVTable`; add new `LRHICommandQueueVTable`, `LRHIFenceVTable`, and their base structs |
| `src/luminary_rhi.c` | Add dispatcher functions that vtable-dispatch each public function |
| `src/luminary_rhi_metal3.m` | Add Metal 3 structs, vtable registrations, and implementations |
| `src/luminary_rhi_metal4.m` | Add Metal 4 structs, vtable registrations, and implementations |

---

## Step 1 — Public API (`luminary_rhi.h`)

Remove the fence and command queue entries from the TODO block and add declarations:

```c
// Command Queue
void lrhi_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error);
void lrhi_destroy_command_queue(LRHICommandQueue queue);
void lrhi_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error);
void lrhi_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);

// Fence
void     lrhi_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error);
void     lrhi_destroy_fence(LRHIFence fence);
uint64_t lrhi_fence_get_value(LRHIFence fence);
void     lrhi_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error);    // CPU-side signal
void     lrhi_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error); // CPU-side wait
```

---

## Step 2 — Internal VTables (`luminary_rhi_internal.h`)

### Extend `LRHIDeviceVTable` (add two new entries after `buffer_readback`):
```c
void (*create_command_queue)(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error);
void (*create_fence)(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error);
```

### Add two new vtables + base structs:
```c
typedef struct LRHICommandQueueVTable {
    void (*destroy_command_queue)(LRHICommandQueue queue);
    void (*signal_fence)(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error);
    void (*wait_fence)(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);
} LRHICommandQueueVTable;

typedef struct LRHIFenceVTable {
    void     (*destroy_fence)(LRHIFence fence);
    uint64_t (*get_value)(LRHIFence fence);
    void     (*signal)(LRHIFence fence, uint64_t value, LRHIError* out_error);
    void     (*wait)(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);
} LRHIFenceVTable;

typedef struct LRHICommandQueueBase { const LRHICommandQueueVTable* vtable; } LRHICommandQueueBase;
typedef struct LRHIFenceBase        { const LRHIFenceVTable*        vtable; } LRHIFenceBase;
```

---

## Step 3 — Dispatcher (`luminary_rhi.c`)

Nine new functions, all following the existing vtable-dispatch pattern. `create_*` go through `LRHIDeviceBase`, all others through their object's base:

```c
void lrhi_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error) {
    ((LRHIDeviceBase*)device)->vtable->create_command_queue(device, out_queue, out_error);
}
void lrhi_destroy_command_queue(LRHICommandQueue queue) {
    ((LRHICommandQueueBase*)queue)->vtable->destroy_command_queue(queue);
}
void lrhi_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error) {
    ((LRHICommandQueueBase*)queue)->vtable->signal_fence(queue, fence, value, out_error);
}
void lrhi_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error) {
    ((LRHICommandQueueBase*)queue)->vtable->wait_fence(queue, fence, value, timeout_ns, out_error);
}
void lrhi_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error) {
    ((LRHIDeviceBase*)device)->vtable->create_fence(device, initial_value, out_fence, out_error);
}
void lrhi_destroy_fence(LRHIFence fence) {
    ((LRHIFenceBase*)fence)->vtable->destroy_fence(fence);
}
uint64_t lrhi_fence_get_value(LRHIFence fence) {
    return ((LRHIFenceBase*)fence)->vtable->get_value(fence);
}
void lrhi_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error) {
    ((LRHIFenceBase*)fence)->vtable->signal(fence, value, out_error);
}
void lrhi_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error) {
    ((LRHIFenceBase*)fence)->vtable->wait(fence, value, timeout_ns, out_error);
}
```

---

## Step 4 — Metal 3 Implementation (`luminary_rhi_metal3.m`)

### New structs:
```objc
typedef struct LRHIFenceWaiterMetal3 {
    dispatch_semaphore_t         semaphore;
    uint64_t                     target_value;
    struct LRHIFenceWaiterMetal3* next;   // intrusive linked list
} LRHIFenceWaiterMetal3;

typedef struct LRHICommandQueueMetal3 {
    LRHICommandQueueBase base;
    id<MTLCommandQueue>  queue;
    id<MTLDevice>        device; // retained for creating command buffers
} LRHICommandQueueMetal3;

typedef struct LRHIFenceMetal3 {
    LRHIFenceBase           base;
    _Atomic uint64_t        value;
    pthread_mutex_t         waiters_mutex;  // protects the waiters list only
    LRHIFenceWaiterMetal3*  waiters;
} LRHIFenceMetal3;
```

No `pthread_cond_t` needed — each waiter carries its own `dispatch_semaphore_t`.

### Shared helper — `metal3_fence_update_value(fence, new_value)`:
Called from both the completion handler and CPU signal path:
1. CAS-loop `fence->value` upward to `new_value` (monotonic).
2. Lock `waiters_mutex`, walk the list: for each waiter whose `target_value <= current_value`, call `dispatch_semaphore_signal` and unlink it from the list.
3. Unlock.

### Key implementations:

**`create_command_queue`**: calls `[device newCommandQueue]`, allocates and initialises the struct.

**`create_fence`**: allocates struct, `atomic_init(&value, initial_value)`, `pthread_mutex_init(&waiters_mutex)`, `waiters = NULL`.

**`destroy_fence`**: `pthread_mutex_destroy`, then `free`. (Any unfinished waiters would be a programmer error; no special handling needed.)

**`command_queue_signal` (GPU → fence)**:  
Creates an empty `MTLCommandBuffer`, attaches `addCompletedHandler` that calls `metal3_fence_update_value(fence, value)`. Commits the command buffer immediately. This is the "completion handler" path for Metal 3.

**`command_queue_wait` (fence → GPU queue)**:  
Metal 3 has no native queue-level event wait. CPU-blocks by calling `fence_wait` internally, stalling the submission thread until the fence reaches the value. Known limitation vs. Metal 4.

**`fence_signal` (CPU → fence)**:  
Calls `metal3_fence_update_value` directly.

**`fence_wait` (CPU waits on fence)**:
```c
// fast path — already reached
if (atomic_load(&fence->value) >= value) return;

dispatch_semaphore_t sem = dispatch_semaphore_create(0);
LRHIFenceWaiterMetal3 waiter = { .semaphore = sem, .target_value = value };

pthread_mutex_lock(&fence->waiters_mutex);
// re-check under lock to close the race window
if (atomic_load(&fence->value) >= value) {
    pthread_mutex_unlock(&fence->waiters_mutex);
    return;
}
waiter.next = fence->waiters;
fence->waiters = &waiter;      // stack-allocated; safe because we wait below
pthread_mutex_unlock(&fence->waiters_mutex);

dispatch_time_t deadline = (timeout_ns == UINT64_MAX)
    ? DISPATCH_TIME_FOREVER
    : dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_ns);

if (dispatch_semaphore_wait(sem, deadline) != 0) {
    // Timed out — remove from the waiters list before returning
    pthread_mutex_lock(&fence->waiters_mutex);
    // unlink &waiter from fence->waiters
    pthread_mutex_unlock(&fence->waiters_mutex);
    // set timeout warning in out_error
}
```

**`fence_get_value`**: `return atomic_load_explicit(&fence->value, memory_order_acquire);`

### VTable registrations:
- Add `create_command_queue` and `create_fence` to `lrhi_metal3_device_vtable`.
- Register `lrhi_metal3_command_queue_vtable` and `lrhi_metal3_fence_vtable`.

---

## Step 5 — Metal 4 Implementation (`luminary_rhi_metal4.m`)

### New structs:
```objc
typedef struct LRHICommandQueueMetal4 {
    LRHICommandQueueBase  base;
    id<MTL4CommandQueue>  queue;   // MTL4CommandQueue protocol
    id<MTLDevice>         device;
} LRHICommandQueueMetal4;

typedef struct LRHIFenceMetal4 {
    LRHIFenceBase        base;
    id<MTLSharedEvent>   event;
} LRHIFenceMetal4;
```

### Key implementations:

**`create_command_queue`**: Casts `MTLDevice.newCommandQueue` return to `id<MTL4CommandQueue>`.  
(Device vtable already validated `MTLGPUFamilyMetal4` support at creation time.)

**`create_fence`**: Creates an `MTLSharedEvent` via `[device newSharedEvent]`, sets `event.signaledValue = initial_value`.

**`destroy_fence`**: just `free`.

**`command_queue_signal` (GPU → fence)**:
```objc
[(id<MTL4CommandQueue>)queue->queue signalEvent:fence->event value:value];
```

**`command_queue_wait` (fence → GPU queue)**:
```objc
[(id<MTL4CommandQueue>)queue->queue waitForEvent:fence->event value:value];
```

**`fence_signal` (CPU → fence)**:
```objc
fence->event.signaledValue = value;
```

**`fence_wait` (CPU waits on fence)**:  
Uses `MTLSharedEventListener` + `dispatch_semaphore_t`:
```objc
dispatch_semaphore_t sem = dispatch_semaphore_create(0);
MTLSharedEventListener* listener = [[MTLSharedEventListener alloc] init];
[fence->event notifyListener:listener atValue:value block:^(id<MTLSharedEvent> e, uint64_t v) {
    dispatch_semaphore_signal(sem);
}];
dispatch_time_t deadline = (timeout_ns == UINT64_MAX)
    ? DISPATCH_TIME_FOREVER
    : dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_ns);
if (dispatch_semaphore_wait(sem, deadline) != 0 && out_error) {
    snprintf(out_error->message, sizeof(out_error->message), "Fence wait timed out");
    out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_WARNING;
}
```

**`fence_get_value`**: `return fence->event.signaledValue;`

### VTable registrations:
- Add `create_command_queue` and `create_fence` to `lrhi_metal4_device_vtable`.
- Register `lrhi_metal4_command_queue_vtable` and `lrhi_metal4_fence_vtable`.

---

## Verification

1. Build: `xmake build` — should compile cleanly with no new warnings.
2. Add a new test in `tests/tests/` that:
   - Creates a device, command queue, and fence with initial value 0.
   - Calls `command_queue_signal(queue, fence, 1)`.
   - Calls `fence_wait(fence, 1, timeout_ns=5s)` from CPU.
   - Verifies `fence_get_value()` returns 1.
   - Calls `fence_signal(fence, 2)` from CPU and verifies `get_value` returns 2.
3. Metal 4 only: also verify `command_queue_wait` orders GPU work correctly (signal with value N, wait for value N, signal with N+1 from GPU, confirm CPU sees N+1 after waiting on N+1).
