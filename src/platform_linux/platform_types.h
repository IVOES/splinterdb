// Copyright 2018-2021 VMware, Inc.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM_LINUX_TYPES_H
#define PLATFORM_LINUX_TYPES_H

#include <assert.h>
#include <ctype.h> // for isspace,isascii,isdigit,isalpha,isupper
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <xxhash.h>

// platform status
typedef typeof(EINVAL) internal_platform_status;

#define STATUS_OK             CONST_STATUS(0)
#define STATUS_NO_MEMORY      CONST_STATUS(ENOMEM)
#define STATUS_BUSY           CONST_STATUS(EAGAIN)
#define STATUS_LIMIT_EXCEEDED CONST_STATUS(ENOSPC)
#define STATUS_NO_SPACE       CONST_STATUS(ENOSPC)
#define STATUS_TIMEDOUT       CONST_STATUS(ETIMEDOUT)
#define STATUS_NO_PERMISSION  CONST_STATUS(EPERM)
#define STATUS_BAD_PARAM      CONST_STATUS(EINVAL)
#define STATUS_INVALID_STATE  CONST_STATUS(EINVAL)
#define STATUS_NOT_FOUND      CONST_STATUS(ENOENT)
#define STATUS_IO_ERROR       CONST_STATUS(EIO)
#define STATUS_TEST_FAILED    CONST_STATUS(-1)

// checksums
typedef XXH32_hash_t       checksum32;
typedef XXH64_hash_t       checksum64;
typedef XXH128_hash_t      checksum128;

#define PLATFORM_CACHELINE_SIZE 64
#define PLATFORM_CACHELINE_ALIGNED \
   __attribute__((__aligned__(PLATFORM_CACHELINE_SIZE)))

 /*
  *   Helper macro that causes branch prediction to favour the likely
  *   side of a jump instruction. If the prediction is correct,
  *   the jump instruction takes zero cycles. If it's wrong, the
  *   processor pipeline needs to be flushed and it can cost
  *   several cycles.
  */
#define LIKELY(_exp)     __builtin_expect(!!(_exp), 1)
#define UNLIKELY(_exp)   __builtin_expect(!!(_exp), 0)

typedef FILE* platform_log_handle;
typedef FILE* platform_stream_handle;

typedef sem_t platform_semaphore;

typedef void* List_Links;

#define STRINGIFY(x) #x
#define STRINGIFY_VALUE(s) STRINGIFY(s)
#define FRACTION_FMT(w, s) "%"STRINGIFY_VALUE(w)"."STRINGIFY_VALUE(s)"f"
#define FRACTION_ARGS(f) ((double)(f).numerator / (double)(f).denominator)


/*
 * Linux understands that you cannot continue after a failed assert already,
 * so we do not need a workaround for platform_assert in linux
 */
#define platform_assert( expr ) assert(expr)

typedef pthread_t platform_thread;

// Mutex
typedef pthread_mutex_t platform_mutex;

// Spin lock
typedef pthread_spinlock_t platform_spinlock;

// Buffer handle
typedef struct {
   void              *addr;
   size_t             length;
} buffer_handle;

// iohandle for laio
typedef struct laio_handle platform_io_handle;

typedef void* platform_module_id;
typedef void* platform_heap_handle;
typedef void* platform_heap_id;

typedef struct {
   unsigned int num_buckets;
   const long* bucket_limits;
   long min, max, total;
   unsigned long num; // no. of elements
   unsigned long count[];
} *platform_histo_handle;

#define UNUSED_PARAM(_parm) _parm  __attribute__((__unused__))
#define UNUSED_TYPE(_parm) UNUSED_PARAM(_parm)
/* #define MUST_CHECK_RESULT   __attribute__((warn_unused_result)) */
#define MUST_CHECK_RESULT

#define ROUNDUP(x,y)    (((x) + (y) - 1) / (y) * (y))
#define ROUNDDOWN(x,y)  ((x) / (y) * (y))

typedef struct platform_condvar {
   pthread_mutex_t lock;
   pthread_cond_t  cond;
} platform_condvar;

#endif
