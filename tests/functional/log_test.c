// Copyright 2018-2021 VMware, Inc.
// SPDX-License-Identifier: Apache-2.0

/*
 * log_test.c --
 *
 *     This file contains tests for Alex's log
 */
#include "platform.h"

#include "log.h"
#include "shard_log.h"
#include "io.h"
#include "allocator.h"
#include "rc_allocator.h"
#include "cache.h"
#include "clockcache.h"
#include "trunk.h"
#include "test.h"

#include "poison.h"

int
test_log_crash(clockcache          *cc,
               clockcache_config   *cache_cfg,
               io_handle           *io,
               allocator           *al,
               shard_log_config    *cfg,
               shard_log           *log,
               uint64               num_entries,
               task_system         *ts,
               platform_heap_handle hh,
               platform_heap_id     hid,
               bool                 crash)

{
   platform_status rc;
   log_handle     *logh;
   uint64          i;
   char            keybuffer[MAX_KEY_SIZE];
   char           *databuffer =
      TYPED_ARRAY_MALLOC(hid, databuffer, cfg->data_cfg->message_size);
   slice              returned_key;
   slice              returned_message;
   char               dummy = 'z';
   uint64             addr;
   uint64             magic;
   shard_log_iterator itor;
   iterator          *itorh = (iterator *)&itor;
   char               key_str[128];
   char               data_str[128];
   bool               at_end;

   platform_assert(cc != NULL);
   rc = shard_log_init(log, (cache *)cc, cfg);
   platform_assert_status_ok(rc);
   logh = (log_handle *)log;

   addr  = log_addr(logh);
   magic = log_magic(logh);


   for (i = 0; i < num_entries; i++) {
      test_key(keybuffer, TEST_RANDOM, i, 0, 0, cfg->data_cfg->key_size, 0);
      test_insert_data(databuffer,
                       1,
                       &dummy,
                       0,
                       cfg->data_cfg->message_size,
                       MESSAGE_TYPE_INSERT);
      slice skey = slice_create(1 + (i % cfg->data_cfg->key_size), keybuffer);
      slice smessage =
         slice_create(1 + ((7 + i) % cfg->data_cfg->message_size), databuffer);
      log_write(logh, skey, smessage, i);
   }

   if (crash) {
      clockcache_deinit(cc);
      rc = clockcache_init(cc,
                           cache_cfg,
                           io,
                           al,
                           "crashed",
                           ts,
                           hh,
                           hid);
      platform_assert_status_ok(rc);
   }

   rc = shard_log_iterator_init((cache *)cc, cfg, hid, addr, magic, &itor);
   platform_assert_status_ok(rc);
   itorh = (iterator *)&itor;

   iterator_at_end(itorh, &at_end);
   for (i = 0; i < num_entries && !at_end; i++) {
      test_key(keybuffer, TEST_RANDOM, i, 0, 0, cfg->data_cfg->key_size, 0);
      test_insert_data(databuffer,
                       1,
                       &dummy,
                       0,
                       cfg->data_cfg->message_size,
                       MESSAGE_TYPE_INSERT);
      slice skey = slice_create(1 + (i % cfg->data_cfg->key_size), keybuffer);
      slice smessage =
         slice_create(1 + ((7 + i) % cfg->data_cfg->message_size), databuffer);
      iterator_get_curr(itorh, &returned_key, &returned_message);
      if (slice_lex_cmp(skey, returned_key)
          || slice_lex_cmp(smessage, returned_message))
      {
         platform_log("log_test_basic: key or data mismatch\n");
         data_key_to_string(cfg->data_cfg, skey, key_str, 128);
         data_message_to_string(cfg->data_cfg, smessage, data_str, 128);
         platform_log("expected: %s -- %s\n", key_str, data_str);
         data_key_to_string(cfg->data_cfg, returned_key, key_str, 128);
         data_message_to_string(cfg->data_cfg, returned_message, data_str, 128);
         platform_log("actual: %s -- %s\n", key_str, data_str);
         platform_assert(0);
      }
      iterator_advance(itorh);
      iterator_at_end(itorh, &at_end);
   }

   platform_log("log returned %lu of %lu entries\n", i, num_entries);

   shard_log_iterator_deinit(hid, &itor);
   shard_log_zap(log);

   platform_free(hid, databuffer);
   return 0;
}

typedef struct test_log_thread_params {
   shard_log      *log;
   platform_thread thread;
   int             thread_id;
   uint64          num_entries;
} test_log_thread_params;

void
test_log_thread(void *arg)
{
   platform_heap_id        hid    = platform_get_heap_id();
   test_log_thread_params *params = (test_log_thread_params *)arg;

   shard_log  *log         = params->log;
   log_handle *logh        = (log_handle *)log;
   int         thread_id   = params->thread_id;
   uint64      num_entries = params->num_entries;
   uint64      i;
   char        key[MAX_KEY_SIZE];
   char *data = TYPED_ARRAY_MALLOC(hid, data, log->cfg->data_cfg->message_size);
   char  dummy;

   slice skey     = slice_create(log->cfg->data_cfg->key_size, key);
   slice smessage = slice_create(log->cfg->data_cfg->message_size, data);

   for (i = thread_id * num_entries; i < (thread_id + 1) * num_entries; i++) {
      test_key(key, TEST_RANDOM, i, 0, 0, log->cfg->data_cfg->key_size, 0);
      test_insert_data(data,
                       1,
                       &dummy,
                       0,
                       log->cfg->data_cfg->message_size,
                       MESSAGE_TYPE_INSERT);
      log_write(logh, skey, smessage, i);
   }

   platform_free(hid, data);
}

platform_status
test_log_perf(cache            *cc,
              shard_log_config *cfg,
              shard_log        *log,
              uint64            num_entries,
              uint64            num_threads,
              task_system      *ts,
              platform_heap_id  hid)

{
   test_log_thread_params *params =
      TYPED_ARRAY_MALLOC(hid, params, num_threads);
   platform_assert(params);
   uint64          start_time;
   platform_status ret;

   ret = shard_log_init(log, (cache *)cc, cfg);
   platform_assert_status_ok(ret);

   for (uint64 i = 0; i < num_threads; i++) {
      params[i].log         = log;
      params[i].thread_id   = i;
      params[i].num_entries = num_entries / num_threads;
   }

   start_time = platform_get_timestamp();
   for (uint64 i = 0; i < num_threads; i++) {
      ret = task_thread_create("log_thread",
                               test_log_thread,
                               &params[i],
                               0,
                               ts,
                               hid,
                               &params[i].thread);
      if (!SUCCESS(ret)) {
         // Wait for existing threads to quit
         for (uint64 j = 0; j < i; j++) {
            platform_thread_join(params[i].thread);
         }
         goto cleanup;
      }
   }
   for (uint64 i = 0; i < num_threads; i++) {
      platform_thread_join(params[i].thread);
   }

   platform_log("log insertion rate: %luM insertions/second\n",
                SEC_TO_MSEC(num_entries)
                   / platform_timestamp_elapsed(start_time));

cleanup:
   platform_free(hid, params);

   return ret;
}


static void
usage(const char *argv0)
{
   platform_error_log("Usage:\n"
                      "\t%s\n"
                      "\t%s --perf\n"
                      "\t%s --crash\n",
                      argv0,
                      argv0,
                      argv0);
   config_usage();
}

int
log_test(int argc, char *argv[])
{
   platform_status     status;
   data_config         data_cfg;
   io_config           io_cfg;
   rc_allocator_config al_cfg;
   clockcache_config   cache_cfg;
   shard_log_config    log_cfg;
   rc_allocator        al;
   platform_status     ret;
   int                 config_argc;
   char              **config_argv;
   bool                run_perf_test;
   bool                run_crash_test;
   int                 rc;
   uint64              seed;
   task_system        *ts;

   if (argc > 1 && strncmp(argv[1], "--perf", sizeof("--perf")) == 0) {
      run_perf_test  = TRUE;
      run_crash_test = FALSE;
      config_argc    = argc - 2;
      config_argv    = argv + 2;
   } else if (argc > 1 && strncmp(argv[1], "--crash", sizeof("--crash")) == 0) {
      run_perf_test  = FALSE;
      run_crash_test = TRUE;
      config_argc    = argc - 2;
      config_argv    = argv + 2;
   } else {
      run_perf_test  = FALSE;
      run_crash_test = FALSE;
      config_argc    = argc - 1;
      config_argv    = argv + 1;
   }

   platform_log("\nStarted log_test!!\n");

   // Create a heap for io, allocator, cache and splinter
   platform_heap_handle hh;
   platform_heap_id     hid;
   status = platform_heap_create(1 * GiB, &hh, &hid);
   platform_assert_status_ok(status);

   trunk_config *cfg = TYPED_MALLOC(hid, cfg);

   status = test_parse_args(cfg,
                            &data_cfg,
                            &io_cfg,
                            &al_cfg,
                            &cache_cfg,
                            &log_cfg,
                            &seed,
                            config_argc,
                            config_argv);
   if (!SUCCESS(status)) {
      platform_error_log("log_test: failed to parse config: %s\n",
                         platform_status_to_string(status));
      /*
       * Provided arguments but set things up incorrectly.
       * Print usage so client can fix commandline.
       */
      usage(argv[0]);
      rc = -1;
      goto cleanup;
   }

   platform_io_handle *io = TYPED_MALLOC(hid, io);
   platform_assert(io != NULL);
   status = io_handle_init(io, &io_cfg, hh, hid);
   if (!SUCCESS(status)) {
      rc = -1;
      goto free_iohandle;
   }

   uint8 num_bg_threads[NUM_TASK_TYPES] = {0}; // no bg threads
   status =
      test_init_splinter(hid, io, &ts, cfg->use_stats, FALSE, num_bg_threads);
   if (!SUCCESS(status)) {
      platform_error_log("Failed to init splinter state: %s\n",
                         platform_status_to_string(status));
      rc = -1;
      goto deinit_iohandle;
   }

   status = rc_allocator_init(&al, &al_cfg, (io_handle *)io, hh, hid);
   platform_assert_status_ok(status);

   clockcache *cc = TYPED_MALLOC(hid, cc);
   platform_assert(cc != NULL);
   status = clockcache_init(cc,
                            &cache_cfg,
                            (io_handle *)io,
                            (allocator *)&al,
                            "test",
                            ts,
                            hh,
                            hid);
   platform_assert_status_ok(status);

   shard_log *log = TYPED_MALLOC(hid, log);
   platform_assert(log != NULL);
   if (run_perf_test) {
      ret = test_log_perf((cache *)cc, &log_cfg, log, 200000000, 16, ts, hid);
      rc  = -1;
      platform_assert_status_ok(ret);
   } else if (run_crash_test) {
      rc = test_log_crash(cc,
                          &cache_cfg,
                          (io_handle *)io,
                          (allocator *)&al,
                          &log_cfg,
                          log,
                          500000,
                          ts,
                          hh,
                          hid,
                          TRUE /* crash */);
      platform_assert(rc == 0);
   } else {
      rc = test_log_crash(cc,
                          &cache_cfg,
                          (io_handle *)io,
                          (allocator *)&al,
                          &log_cfg,
                          log,
                          500000,
                          ts,
                          hh,
                          hid,
                          FALSE /* don't cash */);
      platform_assert(rc == 0);
   }

   clockcache_deinit(cc);
   platform_free(hid, log);
   platform_free(hid, cc);
   rc_allocator_deinit(&al);
   test_deinit_splinter(hid, ts);
deinit_iohandle:
   io_handle_deinit(io);
free_iohandle:
   platform_free(hid, io);
cleanup:
   platform_free(hid, cfg);
   platform_heap_destroy(&hh);

   return rc == 0 ? 0 : -1;
}
