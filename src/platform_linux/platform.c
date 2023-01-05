// Copyright 2018-2021 VMware, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "platform.h"
#include "context.h"

#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>

__thread threadid xxxtid = INVALID_TID;

bool platform_use_hugetlb = FALSE;
bool platform_use_mlock = FALSE;

platform_status
platform_heap_create(platform_module_id UNUSED_PARAM(module_id),
                     uint32 max,
                     platform_heap_handle *heap_handle,
                     platform_heap_id *heap_id)
{
   *heap_handle = NULL;
   *heap_id = NULL;
   return STATUS_OK;
}

void
platform_heap_destroy(platform_heap_handle UNUSED_PARAM(*heap_handle)) {}


int
platform_get_fd(char* filepath, size_t length)
{
        struct stat st;
        int fd;
        if (stat(filepath, &st) < 0) { // need to create file
                fd = open(filepath, O_RDWR | O_CREAT | O_EXCL, 0644);
                assert(fd != -1);
                int s = ftruncate(fd, length);
                assert(s != -1);
        } else {
                //assert(st.st_size == length);
                fd = open(filepath, O_RDWR, 0644);
                assert(fd != -1);
        }

        return fd;
}


buffer_handle *
platform_buffer_create(size_t length,
                       platform_heap_handle UNUSED_PARAM(heap_handle),
                       platform_module_id UNUSED_PARAM(module_id),
		       char* pathname)
{
   buffer_handle *bh = TYPED_MALLOC(platform_get_heap_id(), bh);

   if (bh != NULL) {
      int prot= PROT_READ | PROT_WRITE;
      if ((pathname != NULL) &&
          (strncmp(pathname, "/mnt/pmem0/", 10) == 0)) { // allocate PMEM cache
         int fd    = platform_get_fd(pathname, length);
         int flags = MAP_PRIVATE | MAP_SHARED_VALIDATE | MAP_SYNC;

         bh->addr = mmap(NULL, length, prot, flags, fd, 0);
         platform_log("Persistent cache base addr = %p \n", bh->addr);
      } else { // allocate from DRAM cache
         int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
         if (platform_use_hugetlb) {
            flags |= MAP_HUGETLB;
         }

         bh->addr = mmap(NULL, length, prot, flags, -1, 0);
         if (pathname != NULL) {
            platform_log("Volatile cache base addr = %p \n", bh->addr);
         }

        if (platform_use_mlock) {
           int rc = mlock(bh->addr, length);
           if (rc != 0) {
              platform_error_log("mlock (%lu) failed with error: %s\n", length,
                                 strerror(errno));
              munmap(bh->addr, length);
              goto error;
           }
        }
      }
      if (bh->addr == MAP_FAILED) {
         platform_error_log("mmap (%lu) failed with error: %s\n", length,
                            strerror(errno));
         goto error;
      }

   }

   bh->length = length;
   return bh;

error:
   platform_free(platform_get_heap_id(), bh);
   bh = NULL;
   return bh;
}

void *
platform_buffer_getaddr(const buffer_handle *bh)
{
   return bh->addr;
}

platform_status
platform_buffer_destroy(buffer_handle *bh)
{
   int ret;
   ret = munmap(bh->addr, bh->length);

   if (ret != 0) {
      platform_free(platform_get_heap_id(), bh);
   }

   return CONST_STATUS(ret);
}

platform_status
platform_thread_create(platform_thread       *thread,
                       bool                   detached,
                       platform_thread_worker worker,
                       void                  *arg,
                       platform_heap_id       UNUSED_PARAM(heap_id))
{
   int ret;

   if (detached) {
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      size_t stacksize = 16UL * 1024UL;
      pthread_attr_setstacksize(&attr, stacksize);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      ret = pthread_create(thread, &attr, (void *(*)(void *))worker, arg);
      pthread_attr_destroy(&attr);
   } else {
      ret = pthread_create(thread, NULL,  (void *(*)(void *))worker, arg);
   }

  return CONST_STATUS(ret);
}

platform_status
platform_thread_join(platform_thread thread)
{
   int ret;
   void *retval;

   ret = pthread_join(thread, &retval);

   return CONST_STATUS(ret);
}

platform_status
platform_mutex_init(platform_mutex *mu,
                    platform_module_id UNUSED_PARAM(module_id),
                    platform_heap_id UNUSED_PARAM(heap_id))
{
   int ret;

   ret = pthread_mutex_init(mu, NULL);

   return CONST_STATUS(ret);
}

platform_status
platform_mutex_destroy(platform_mutex *mu)
{
   int ret;

   ret = pthread_mutex_destroy(mu);

   return CONST_STATUS(ret);
}

platform_status
platform_spinlock_init(platform_spinlock *lock,
                       platform_module_id UNUSED_PARAM(module_id),
                       platform_heap_id   UNUSED_PARAM(heap_id))
{
   int ret;

   ret = pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE);

   return CONST_STATUS(ret);
}

platform_status
platform_spinlock_destroy(platform_spinlock *lock)
{
   int ret;

   ret = pthread_spin_destroy(lock);

   return CONST_STATUS(ret);
}

void
platform_batch_rwlock_init(platform_batch_rwlock *lock)
{
   ZERO_CONTENTS(lock);
}

bool
platform_batch_rwlock_try_writelock(platform_batch_rwlock *lock, uint64 lock_idx)
{
   if (__sync_lock_test_and_set(&lock->write_lock[lock_idx], 1)) {
      return FALSE;
   }
   uint64 wait = 1;
   threadid tid = platform_get_tid();
   for (uint64 i = 0; i < MAX_THREADS; i++) {
      while (i != tid && lock->read_counter[i][lock_idx] != 0) {
         platform_sleep(wait);
         wait = wait > 2048 ? wait : 2 * wait;
      }
      wait = 1;
   }
   return TRUE;
}


void
platform_batch_rwlock_writelock(platform_batch_rwlock *lock, uint64 lock_idx)
{
   uint64 wait = 1;
   while (__sync_lock_test_and_set(&lock->write_lock[lock_idx], 1)) {
      platform_sleep(wait);
      wait = wait > 2048 ? wait : 2 * wait;
   }
   wait = 1;
   for (uint64 i = 0; i < MAX_THREADS; i++) {
      while (lock->read_counter[i][lock_idx] != 0) {
         platform_sleep(wait);
         wait = wait > 2048 ? wait : 2 * wait;
      }
      wait = 1;
   }
}

void
platform_batch_rwlock_unwritelock(platform_batch_rwlock *lock, uint64 lock_idx)
{
   __sync_lock_release(&lock->write_lock[lock_idx]);
}

void
platform_batch_rwlock_readlock(platform_batch_rwlock *lock, uint64 lock_idx)
{
   threadid tid = platform_get_tid();
   while (1) {
      uint64 wait = 1;
      while (lock->write_lock[lock_idx]) {
         platform_sleep(wait);
         wait = wait > 2048 ? wait : 2 * wait;
      }
      debug_only uint8 old_counter = __sync_fetch_and_add(&lock->read_counter[tid][lock_idx], 1);
      debug_assert(old_counter == 0);
      if (!lock->write_lock[lock_idx]) {
         return;
      }
      old_counter = __sync_fetch_and_sub(&lock->read_counter[tid][lock_idx], 1);
      debug_assert(old_counter == 1);
   }
   platform_assert(0);
}

void
platform_batch_rwlock_unreadlock(platform_batch_rwlock *lock, uint64 lock_idx)
{
   threadid tid = platform_get_tid();
   debug_only uint8 old_counter = __sync_fetch_and_sub(&lock->read_counter[tid][lock_idx], 1);
   debug_assert(old_counter == 1);
}

platform_status
platform_histo_create(platform_heap_id heap_id,
                      uint32 num_buckets,
                      const int64* const bucket_limits,
                      platform_histo_handle *histo)
{
   platform_histo_handle hh;
   hh = TYPED_MALLOC_MANUAL(heap_id, hh, sizeof(hh) +
                            num_buckets * sizeof(hh->count[0]));
   if (!hh) {
      return STATUS_NO_MEMORY;
   }
   hh->num_buckets = num_buckets;
   hh->bucket_limits = bucket_limits;
   hh->total = 0;
   hh->min = INT64_MAX;
   hh->max = INT64_MIN;
   hh->num = 0;
   memset(hh->count, 0, hh->num_buckets * sizeof(hh->count[0]));

   *histo = hh;
   return STATUS_OK;
}

void
platform_histo_destroy(platform_heap_id heap_id,
                       platform_histo_handle histo)
{
   platform_assert(histo);
   platform_free(heap_id, histo);
}

void
platform_histo_print(platform_histo_handle histo, const char *name)
{
   if (histo->num == 0) {
      return;
   }

   platform_log("%s\n", name);
   platform_log("min: %ld\n", histo->min);
   platform_log("max: %ld\n", histo->max);
   platform_log("mean: %ld\n", histo->num == 0 ? 0 :
                histo->total / histo->num);
   platform_log("count: %ld\n", histo->num);
   for (uint32 i = 0; i < histo->num_buckets; i++) {
      if (i == histo->num_buckets - 1) {
         platform_log("%-12ld  > %12ld\n", histo->count[i],
                      histo->bucket_limits[i - 1]);
      } else {
         platform_log("%-12ld <= %12ld\n", histo->count[i],
                      histo->bucket_limits[i]);
      }
   }
   platform_log("\n");
}

char *
platform_strtok_r(char *str, const char *delim, platform_strtok_ctx *ctx)
{
   return strtok_r(str, delim, &ctx->token_str);
}

void
platform_sort_slow(void *base,
                   size_t nmemb,
                   size_t size,
                   platform_sort_cmpfn cmpfn,
                   void *cmparg,
                   void *temp)
{
   return qsort_r(base, nmemb, size, cmpfn, cmparg);
}

platform_status
platform_condvar_init(platform_condvar *cv, platform_heap_id heap_id)
{
   platform_status status;

   status = platform_mutex_init(&cv->lock, platform_get_module_id(), heap_id);
   if (!SUCCESS(status)) {
      return status;
   }

   status.r = pthread_cond_init(&cv->cond, NULL);
   if (!SUCCESS(status)) {
      platform_mutex_destroy(&cv->lock);
   }

   return status;
}

platform_status
platform_condvar_wait(platform_condvar *cv)
{
   int status;

   status = pthread_cond_wait(&cv->cond, &cv->lock);
   return CONST_STATUS(status);
}

platform_status
platform_condvar_signal(platform_condvar *cv)
{
   int status;

   status = pthread_cond_signal(&cv->cond);
   return CONST_STATUS(status);
}

platform_status
platform_condvar_broadcast(platform_condvar *cv)
{
   int status;

   status = pthread_cond_broadcast(&cv->cond);
   return CONST_STATUS(status);
}
