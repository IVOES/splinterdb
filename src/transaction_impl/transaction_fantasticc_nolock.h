#pragma once

#include "splinterdb/data.h"
#include "platform.h"
#include "data_internal.h"
#include "splinterdb/transaction.h"
#include "util.h"
#include "experimental_mode.h"
#include "splinterdb_internal.h"
#include "iceberg_table.h"
#include "poison.h"

/*
 * Merge functions
 */

/*
 * The structure of value to be stored in SplinterDB
 * TODO: The type of `ts` can be uint64.
 */
typedef struct ONDISK tuple_header {
   txn_timestamp is_ts_update : 1;
   txn_timestamp delta : 64;
   txn_timestamp wts : 63;
   char          value[];
} tuple_header;

typedef struct transactional_data_config {
   data_config        super;
   const data_config *application_data_config;
} transactional_data_config;

static inline bool
is_message_timestamps_update(message msg)
{
   tuple_header *tuple = (tuple_header *)message_data(msg);
   return tuple->is_ts_update;
}

static inline bool
is_merge_accumulator_timestamps_update(merge_accumulator *ma)
{
   tuple_header *tuple = (tuple_header *)merge_accumulator_data(ma);
   return tuple->is_ts_update;
}

static inline message
get_app_value_from_message(message msg)
{
   return message_create(
      message_class(msg),
      slice_create(message_length(msg) - sizeof(tuple_header),
                   message_data(msg) + sizeof(tuple_header)));
}

static inline message
get_app_value_from_merge_accumulator(merge_accumulator *ma)
{
   return message_create(
      merge_accumulator_message_class(ma),
      slice_create(merge_accumulator_length(ma) - sizeof(tuple_header),
                   merge_accumulator_data(ma) + sizeof(tuple_header)));
}

static int
merge_fantasticc_tuple(const data_config *cfg,
                       slice              key,         // IN
                       message            old_message, // IN
                       merge_accumulator *new_message) // IN/OUT
{
   if (is_message_timestamps_update(old_message)) {
      // Just discard
      return 0;
   }

   if (is_merge_accumulator_timestamps_update(new_message)) {
      tuple_header *old_tuple = (tuple_header *)message_data(old_message);
      tuple_header *new_tuple =
         (tuple_header *)merge_accumulator_data(new_message);
      old_tuple->is_ts_update = 0;
      old_tuple->delta        = new_tuple->delta;
      old_tuple->wts          = new_tuple->wts;
      merge_accumulator_copy_message(new_message, old_message);
      merge_accumulator_set_class(new_message, message_class(old_message));
      return 0;
   }

   message old_value_message = get_app_value_from_message(old_message);
   message new_value_message =
      get_app_value_from_merge_accumulator(new_message);

   merge_accumulator new_value_ma;
   merge_accumulator_init_from_message(
      &new_value_ma,
      new_message->data.heap_id,
      new_value_message); // FIXME: use a correct heap_id

   data_merge_tuples(
      ((const transactional_data_config *)cfg)->application_data_config,
      key_create_from_slice(key),
      old_value_message,
      &new_value_ma);

   merge_accumulator_resize(new_message,
                            sizeof(tuple_header)
                               + merge_accumulator_length(&new_value_ma));

   tuple_header *new_tuple = merge_accumulator_data(new_message);
   memcpy(&new_tuple->value,
          merge_accumulator_data(&new_value_ma),
          merge_accumulator_length(&new_value_ma));

   merge_accumulator_deinit(&new_value_ma);

   merge_accumulator_set_class(new_message, message_class(old_message));

   return 0;
}

static int
merge_fantasticc_tuple_final(const data_config *cfg,
                             slice              key,
                             merge_accumulator *oldest_message)
{
   platform_assert(!is_merge_accumulator_timestamps_update(oldest_message),
                   "oldest_message shouldn't be a rts update\n");

   message oldest_message_value =
      get_app_value_from_merge_accumulator(oldest_message);
   merge_accumulator app_oldest_message;
   merge_accumulator_init_from_message(
      &app_oldest_message,
      app_oldest_message.data.heap_id, // FIXME: use a correct heap id
      oldest_message_value);

   data_merge_tuples_final(
      ((const transactional_data_config *)cfg)->application_data_config,
      key_create_from_slice(key),
      &app_oldest_message);

   merge_accumulator_resize(oldest_message,
                            sizeof(tuple_header)
                               + merge_accumulator_length(&app_oldest_message));
   tuple_header *tuple = merge_accumulator_data(oldest_message);
   memcpy(&tuple->value,
          merge_accumulator_data(&app_oldest_message),
          merge_accumulator_length(&app_oldest_message));

   merge_accumulator_deinit(&app_oldest_message);

   return 0;
}

static inline void
transactional_data_config_init(data_config               *in_cfg, // IN
                               transactional_data_config *out_cfg // OUT
)
{
   memcpy(&out_cfg->super, in_cfg, sizeof(out_cfg->super));
   out_cfg->super.merge_tuples       = merge_fantasticc_tuple;
   out_cfg->super.merge_tuples_final = merge_fantasticc_tuple_final;
   out_cfg->application_data_config  = in_cfg;
}

typedef struct transactional_splinterdb_config {
   splinterdb_config           kvsb_cfg;
   transactional_data_config  *txn_data_cfg;
   transaction_isolation_level isol_level;
   uint64                      tscache_log_slots;
} transactional_splinterdb_config;

typedef struct transactional_splinterdb {
   splinterdb                      *kvsb;
   transactional_splinterdb_config *tcfg;
   iceberg_table                   *tscache;
} transactional_splinterdb;


// TicToc paper used this structure, but it causes a lot of delta overflow
/* typedef struct { */
/*    txn_timestamp lock_bit : 1; */
/*    txn_timestamp delta : 15; */
/*    txn_timestamp wts : 48; */
/* } timestamp_set __attribute__((aligned(sizeof(txn_timestamp)))); */

typedef struct {
   txn_timestamp lock_bit : 1;
   txn_timestamp delta : 64;
   txn_timestamp wts : 63;
} timestamp_set __attribute__((aligned(sizeof(txn_timestamp))));

static inline bool
timestamp_set_is_equal(const timestamp_set *s1, const timestamp_set *s2)
{
   return memcmp((const void *)s1, (const void *)s2, sizeof(timestamp_set))
          == 0;
}

static inline txn_timestamp
timestamp_set_get_rts(timestamp_set *ts)
{
   return ts->wts + ts->delta;
}

static inline bool
timestamp_set_compare_and_swap(timestamp_set *ts,
                               timestamp_set *v1,
                               timestamp_set *v2)
{
   return __atomic_compare_exchange((volatile txn_timestamp *)ts,
                                    (txn_timestamp *)v1,
                                    (txn_timestamp *)v2,
                                    TRUE,
                                    __ATOMIC_RELAXED,
                                    __ATOMIC_RELAXED);
}

static inline void
timestamp_set_load(timestamp_set *ts, timestamp_set *v)
{
   __atomic_load(
      (volatile txn_timestamp *)ts, (txn_timestamp *)v, __ATOMIC_RELAXED);
}

typedef struct rw_entry {
   slice          key;
   message        msg; // value + op
   txn_timestamp  wts;
   txn_timestamp  rts;
   timestamp_set *tuple_ts;
   bool           is_read;
   bool           need_to_keep_key;
   bool           need_to_decrease_refcount;
} rw_entry;

static inline timestamp_set
timestamp_get_from_splinterdb(const splinterdb *kvsb, slice key)
{
   platform_assert(!slice_is_null(key));
   splinterdb_lookup_result ts_result;
   splinterdb_lookup_result_init(kvsb, &ts_result, 0, NULL);
   platform_assert(!splinterdb_lookup(kvsb, key, &ts_result));
   timestamp_set ts = {0};
   if (splinterdb_lookup_found(&ts_result)) {
      slice value;
      splinterdb_lookup_result_value(&ts_result, &value);
      tuple_header *tuple = (tuple_header *)slice_data(value);
      ts.wts              = tuple->wts;
      ts.delta            = tuple->delta;
   }
   splinterdb_lookup_result_deinit(&ts_result);
   return ts;
}


/*
 * This function has the following effects:
 * A. If entry key is not in the cache, it inserts the key in the cache with
 * refcount=1 and value=0. B. If the key is already in the cache, it just
 * increases the refcount. C. returns the pointer to the value.
 */
static inline bool
rw_entry_iceberg_insert(transactional_splinterdb *txn_kvsb, rw_entry *entry)
{
   // Make sure increasing the refcount only once
   if (entry->tuple_ts) {
      return FALSE;
   }

   KeyType key_ht = (KeyType)slice_data(entry->key);
   // ValueType value_ht = {0};
#if EXPERIMENTAL_MODE_KEEP_ALL_KEYS == 1
   // bool is_new_item = iceberg_insert_without_increasing_refcount(
   //    txn_kvsb->tscache, key_ht, value_ht, platform_get_tid());

   timestamp_set ts = {0};
   entry->tuple_ts  = &ts;
   bool is_new_item = iceberg_insert_and_get_without_increasing_refcount(
      txn_kvsb->tscache,
      key_ht,
      (ValueType **)&entry->tuple_ts,
      platform_get_tid() - 1);
   platform_assert(entry->tuple_ts != &ts);
#else
   // increase refcount for key
   timestamp_set ts = {0};
   entry->tuple_ts  = &ts;
   bool is_new_item = iceberg_insert_and_get(txn_kvsb->tscache,
                                             key_ht,
                                             (ValueType **)&entry->tuple_ts,
                                             platform_get_tid() - 1);
#endif

   // get the pointer of the value from the iceberg
   // platform_assert(iceberg_get_value(txn_kvsb->tscache,
   //                                   key_ht,
   //                                   (ValueType **)&entry->tuple_ts,
   //                                   platform_get_tid()));

   /* platform_assert(((uint64)entry->tuple_ts) % sizeof(txn_timestamp) == 0);
    */

   entry->need_to_keep_key = entry->need_to_keep_key || is_new_item;
   return is_new_item;
}

static inline void
rw_entry_iceberg_remove(transactional_splinterdb *txn_kvsb,
                        rw_entry                 *entry,
                        bool                      upsert_ts)
{
   if (!entry->tuple_ts) {
      return;
   }

   entry->tuple_ts = NULL;

#if !EXPERIMENTAL_MODE_KEEP_ALL_KEYS
   KeyType   key_ht   = (KeyType)slice_data(entry->key);
   ValueType value_ht = {0};
   if (iceberg_get_and_remove(
          txn_kvsb->tscache, &key_ht, &value_ht, platform_get_tid() - 1))
   { // the key is evicted.
      if (upsert_ts) {
         // upsert to update the evicted timestamps
         timestamp_set *ts = (timestamp_set *)&value_ht;
         ts->lock_bit      = 1; // = is_ts_update
         slice delta       = slice_create(sizeof(*ts), ts);
         splinterdb_update(txn_kvsb->kvsb, entry->key, delta);
      }

      if (slice_data(entry->key) != key_ht) {
         platform_free_from_heap(0, key_ht);
      } else {
         entry->need_to_keep_key = 0;
      }
   }
#endif
}

static rw_entry *
rw_entry_create()
{
   rw_entry *new_entry;
   new_entry = TYPED_ZALLOC(0, new_entry);
   platform_assert(new_entry != NULL);
   new_entry->tuple_ts = NULL;
   return new_entry;
}

static inline void
rw_entry_deinit(rw_entry *entry)
{
   bool can_key_free = !slice_is_null(entry->key) && !entry->need_to_keep_key;
   if (can_key_free) {
      platform_free_from_heap(0, (void *)slice_data(entry->key));
   }

   if (!message_is_null(entry->msg)) {
      platform_free_from_heap(0, (void *)message_data(entry->msg));
   }
}

static inline void
rw_entry_set_key(rw_entry *e, slice key, const data_config *cfg)
{
   char *key_buf;
   key_buf = TYPED_ARRAY_ZALLOC(0, key_buf, KEY_SIZE);
   memcpy(key_buf, slice_data(key), slice_length(key));
   e->key = slice_create(KEY_SIZE, key_buf);
}

/*
 * The msg is the msg from app.
 * In EXPERIMENTAL_MODE_TICTOC_DISK, this function adds timestamps at the begin
 * of the msg
 */
static inline void
rw_entry_set_msg(rw_entry *e, message msg)
{
   char        *msg_buf;
   const uint64 tuple_size = sizeof(tuple_header) + message_length(msg);
   msg_buf                 = TYPED_ARRAY_ZALLOC(0, msg_buf, tuple_size);
   memcpy(msg_buf + sizeof(tuple_header), message_data(msg), tuple_size);
   e->msg =
      message_create(message_class(msg), slice_create(tuple_size, msg_buf));
}

static inline bool
rw_entry_is_read(const rw_entry *entry)
{
   return entry->is_read;
}

static inline bool
rw_entry_is_write(const rw_entry *entry)
{
   return !message_is_null(entry->msg);
}

/*
 * Will Set timestamps in entry later
 */
static inline rw_entry *
rw_entry_get(transactional_splinterdb *txn_kvsb,
             transaction              *txn,
             slice                     user_key,
             const data_config        *cfg,
             const bool                is_read)
{
   bool      need_to_create_new_entry = TRUE;
   rw_entry *entry                    = NULL;
   const key ukey                     = key_create_from_slice(user_key);
   for (int i = 0; i < txn->num_rw_entries; ++i) {
      entry = txn->rw_entries[i];

      if (data_key_compare(cfg, ukey, key_create_from_slice(entry->key)) == 0) {
         need_to_create_new_entry = FALSE;
         break;
      }
   }

   if (need_to_create_new_entry) {
      entry = rw_entry_create();
      rw_entry_set_key(entry, user_key, cfg);
      txn->rw_entries[txn->num_rw_entries++] = entry;
   }

   entry->is_read = entry->is_read || is_read;
   return entry;
}

static int
rw_entry_key_compare(const void *elem1, const void *elem2, void *args)
{
   const data_config *cfg = (const data_config *)args;

   rw_entry *e1 = *((rw_entry **)elem1);
   rw_entry *e2 = *((rw_entry **)elem2);

   key akey = key_create_from_slice(e1->key);
   key bkey = key_create_from_slice(e2->key);

   return data_key_compare(cfg, akey, bkey);
}

static inline bool
rw_entry_try_lock(rw_entry *entry)
{
   timestamp_set v1, v2;
   timestamp_set_load(entry->tuple_ts, &v1);
   v2 = v1;
   if (v1.lock_bit) {
      return false;
   }
   v2.lock_bit = 1;
   return timestamp_set_compare_and_swap(entry->tuple_ts, &v1, &v2);
}

static inline void
rw_entry_unlock(rw_entry *entry)
{
   timestamp_set v1, v2;
   do {
      timestamp_set_load(entry->tuple_ts, &v1);
      v2          = v1;
      v2.lock_bit = 0;
   } while (!timestamp_set_compare_and_swap(entry->tuple_ts, &v1, &v2));
}

static void
transactional_splinterdb_config_init(
   transactional_splinterdb_config *txn_splinterdb_cfg,
   const splinterdb_config         *kvsb_cfg)
{
   memcpy(&txn_splinterdb_cfg->kvsb_cfg,
          kvsb_cfg,
          sizeof(txn_splinterdb_cfg->kvsb_cfg));

   txn_splinterdb_cfg->txn_data_cfg =
      TYPED_ZALLOC(0, txn_splinterdb_cfg->txn_data_cfg);
   transactional_data_config_init(kvsb_cfg->data_cfg,
                                  txn_splinterdb_cfg->txn_data_cfg);
   txn_splinterdb_cfg->kvsb_cfg.data_cfg =
      (data_config *)txn_splinterdb_cfg->txn_data_cfg;

   txn_splinterdb_cfg->tscache_log_slots = 20;

   // TODO things like filename, logfile, or data_cfg would need a
   // deep-copy
   txn_splinterdb_cfg->isol_level = TRANSACTION_ISOLATION_LEVEL_SERIALIZABLE;
}

static int
transactional_splinterdb_create_or_open(const splinterdb_config   *kvsb_cfg,
                                        transactional_splinterdb **txn_kvsb,
                                        bool open_existing)
{
   check_experimental_mode_is_valid();
   print_current_experimental_modes();

   transactional_splinterdb_config *txn_splinterdb_cfg;
   txn_splinterdb_cfg = TYPED_ZALLOC(0, txn_splinterdb_cfg);
   transactional_splinterdb_config_init(txn_splinterdb_cfg, kvsb_cfg);

   transactional_splinterdb *_txn_kvsb;
   _txn_kvsb       = TYPED_ZALLOC(0, _txn_kvsb);
   _txn_kvsb->tcfg = txn_splinterdb_cfg;

   int rc = splinterdb_create_or_open(
      &txn_splinterdb_cfg->kvsb_cfg, &_txn_kvsb->kvsb, open_existing);
   bool fail_to_create_splinterdb = (rc != 0);
   if (fail_to_create_splinterdb) {
      platform_free(0, _txn_kvsb);
      platform_free(0, txn_splinterdb_cfg);
      return rc;
   }

   iceberg_table *tscache;
   tscache = TYPED_ZALLOC(0, tscache);
   platform_assert(iceberg_init(tscache, txn_splinterdb_cfg->tscache_log_slots)
                   == 0);
   _txn_kvsb->tscache = tscache;

   *txn_kvsb = _txn_kvsb;

   return 0;
}

int
transactional_splinterdb_create(const splinterdb_config   *kvsb_cfg,
                                transactional_splinterdb **txn_kvsb)
{
   return transactional_splinterdb_create_or_open(kvsb_cfg, txn_kvsb, FALSE);
}


int
transactional_splinterdb_open(const splinterdb_config   *kvsb_cfg,
                              transactional_splinterdb **txn_kvsb)
{
   return transactional_splinterdb_create_or_open(kvsb_cfg, txn_kvsb, TRUE);
}

void
transactional_splinterdb_close(transactional_splinterdb **txn_kvsb)
{
   transactional_splinterdb *_txn_kvsb = *txn_kvsb;

   iceberg_print_state(_txn_kvsb->tscache);

   splinterdb_close(&_txn_kvsb->kvsb);

   platform_free(0, _txn_kvsb->tscache);
   platform_free(0, _txn_kvsb->tcfg);
   platform_free(0, _txn_kvsb);

   *txn_kvsb = NULL;
}

void
transactional_splinterdb_register_thread(transactional_splinterdb *kvs)
{
   splinterdb_register_thread(kvs->kvsb);
}

void
transactional_splinterdb_deregister_thread(transactional_splinterdb *kvs)
{
   splinterdb_deregister_thread(kvs->kvsb);
}

int
transactional_splinterdb_begin(transactional_splinterdb *txn_kvsb,
                               transaction              *txn)
{
   platform_assert(txn);
   memset(txn, 0, sizeof(*txn));
   return 0;
}

static inline void
transaction_deinit(transactional_splinterdb *txn_kvsb, transaction *txn)
{
   for (int i = 0; i < txn->num_rw_entries; ++i) {
      rw_entry_iceberg_remove(txn_kvsb, txn->rw_entries[i], FALSE);
      rw_entry_deinit(txn->rw_entries[i]);
      platform_free(0, txn->rw_entries[i]);
   }
}

int
transactional_splinterdb_commit(transactional_splinterdb *txn_kvsb,
                                transaction              *txn)
{
   txn_timestamp commit_ts = 0;

   int       num_reads                    = 0;
   int       num_writes                   = 0;
   rw_entry *read_set[RW_SET_SIZE_LIMIT]  = {0};
   rw_entry *write_set[RW_SET_SIZE_LIMIT] = {0};

   for (int i = 0; i < txn->num_rw_entries; i++) {
      rw_entry *entry = txn->rw_entries[i];
      if (rw_entry_is_write(entry)) {
         write_set[num_writes++] = entry;
      }

      if (rw_entry_is_read(entry)) {
         read_set[num_reads++] = entry;

         txn_timestamp wts = entry->wts;
#if EXPERIMENTAL_MODE_SILO == 1
         wts += 1;
#endif
         commit_ts = MAX(commit_ts, wts);
      }
   }

   platform_sort_slow(write_set,
                      num_writes,
                      sizeof(rw_entry *),
                      rw_entry_key_compare,
                      (void *)txn_kvsb->tcfg->kvsb_cfg.data_cfg,
                      NULL);

   bool is_new_entries[RW_SET_SIZE_LIMIT] = {0};

RETRY_LOCK_WRITE_SET:
{
   for (int lock_num = 0; lock_num < num_writes; ++lock_num) {
      rw_entry *w = write_set[lock_num];
      platform_assert(w->tuple_ts);
      // if (!w->tuple_ts) {
      //    rw_entry_iceberg_insert(txn_kvsb, w);
      // }

      if (!rw_entry_try_lock(w)) {
         // This is "no-wait" optimization in the TicToc paper.
         for (int i = 0; i < lock_num; ++i) {
            rw_entry_unlock(write_set[i]);
         }

         // 1us is the value that is mentioned in the paper
         platform_sleep_ns(1000);

         goto RETRY_LOCK_WRITE_SET;
      }
   }
}

   for (uint64 i = 0; i < num_writes; ++i) {
      rw_entry *w = write_set[i];
      if (is_new_entries[i]) {
         timestamp_set ts =
            timestamp_get_from_splinterdb(txn_kvsb->kvsb, w->key);
         w->rts = timestamp_set_get_rts(&ts);
      } else {
         w->rts = timestamp_set_get_rts(w->tuple_ts);
      }

      commit_ts = MAX(commit_ts, w->rts + 1);
   }

   bool is_abort = FALSE;
   for (uint64 i = 0; !is_abort && i < num_reads; ++i) {
      rw_entry *r = read_set[i];
      platform_assert(rw_entry_is_read(r));

      if (r->rts < commit_ts) {
         bool          is_success;
         timestamp_set v1, v2;
         do {
            is_success = TRUE;
            timestamp_set_load(r->tuple_ts, &v1);
            v2                                 = v1;
            bool          is_wts_different     = r->wts != v1.wts;
            txn_timestamp rts                  = timestamp_set_get_rts(&v1);
            bool          is_locked_by_another = rts <= commit_ts
                                        && r->tuple_ts->lock_bit
                                        && !rw_entry_is_write(r);
            if (is_wts_different || is_locked_by_another) {
               is_abort = TRUE;
               break;
            }
            if (rts <= commit_ts) {
               txn_timestamp delta = commit_ts - v1.wts;
               /* txn_timestamp shift = delta - (delta & 0x7fff); */
               txn_timestamp shift = delta - (delta & UINT64_MAX);
               platform_assert(shift == 0);
               v2.wts += shift;
               v2.delta = delta - shift;
               is_success =
                  timestamp_set_compare_and_swap(r->tuple_ts, &v1, &v2);
            }
         } while (!is_success);

         // Decrease the refcount and write timestamps back if it is evicted.
         if (!rw_entry_is_write(r)) {
            rw_entry_iceberg_remove(txn_kvsb, r, TRUE);
         }
      }
   }

   if (!is_abort) {
      int rc = 0;

      for (uint64 i = 0; i < num_writes; ++i) {
         rw_entry *w = write_set[i];
         platform_assert(rw_entry_is_write(w));

#if EXPERIMENTAL_MODE_BYPASS_SPLINTERDB == 1
         if (0) {
#endif
            tuple_header *tuple = (tuple_header *)message_data(w->msg);
            tuple->is_ts_update = 0;
            tuple->delta        = 0;
            tuple->wts          = commit_ts;
            switch (message_class(w->msg)) {
               case MESSAGE_TYPE_INSERT:
                  rc = splinterdb_insert(
                     txn_kvsb->kvsb, w->key, message_slice(w->msg));
                  break;
               case MESSAGE_TYPE_UPDATE:
                  rc = splinterdb_update(
                     txn_kvsb->kvsb, w->key, message_slice(w->msg));
                  break;
               case MESSAGE_TYPE_DELETE:
                  rc = splinterdb_delete(txn_kvsb->kvsb, w->key);
                  break;
               default:
                  break;
            }

            platform_assert(rc == 0, "Error from SplinterDB: %d\n", rc);
#if EXPERIMENTAL_MODE_BYPASS_SPLINTERDB == 1
         }
#endif
         timestamp_set v1, v2;
         do {
            timestamp_set_load(w->tuple_ts, &v1);
            v2          = v1;
            v2.wts      = commit_ts;
            v2.delta    = 0;
            v2.lock_bit = 0;
         } while (!timestamp_set_compare_and_swap(w->tuple_ts, &v1, &v2));
      }
   } else {
      for (uint64 i = 0; i < num_writes; ++i) {
         rw_entry_unlock(write_set[i]);
      }
   }

   transaction_deinit(txn_kvsb, txn);

   return (-1 * is_abort);
}

int
transactional_splinterdb_abort(transactional_splinterdb *txn_kvsb,
                               transaction              *txn)
{
   transaction_deinit(txn_kvsb, txn);

   return 0;
}

static int
local_write(transactional_splinterdb *txn_kvsb,
            transaction              *txn,
            slice                     user_key,
            message                   msg)
{
   const data_config *cfg   = txn_kvsb->tcfg->kvsb_cfg.data_cfg;
   const key          ukey  = key_create_from_slice(user_key);
   rw_entry          *entry = rw_entry_get(txn_kvsb, txn, user_key, cfg, FALSE);
   rw_entry_iceberg_insert(txn_kvsb, entry);

   /* if (message_class(msg) == MESSAGE_TYPE_UPDATE */
   /*     || message_class(msg) == MESSAGE_TYPE_DELETE) */
   /* { */
   /*    rw_entry_iceberg_insert(txn_kvsb, entry); */
   /*    timestamp_set v = *entry->tuple_ts; */
   /*    entry->wts      = v.wts; */
   /*    entry->rts      = timestamp_set_get_rts(&v); */
   /* } */
   
   if (message_is_null(entry->msg)) {
      rw_entry_set_msg(entry, msg);
   } else {
      // TODO it needs to be checked later for upsert
      key wkey = key_create_from_slice(entry->key);
      if (data_key_compare(cfg, wkey, ukey) == 0) {
         if (message_is_definitive(msg)) {
            platform_free_from_heap(0, (void *)message_data(entry->msg));
            rw_entry_set_msg(entry, msg);
         } else {
            platform_assert(message_class(entry->msg) != MESSAGE_TYPE_DELETE);

            merge_accumulator new_message;
            merge_accumulator_init_from_message(&new_message, 0, msg);
            data_merge_tuples(cfg, ukey, entry->msg, &new_message);
            platform_free_from_heap(0, (void *)message_data(entry->msg));
            merge_accumulator_resize(
               &new_message,
               sizeof(tuple_header) + merge_accumulator_length(&new_message));
            tuple_header *tuple =
               (tuple_header *)merge_accumulator_data(&new_message);
            memmove(tuple->value,
                    merge_accumulator_data(&new_message),
                    merge_accumulator_length(&new_message));
            entry->msg = merge_accumulator_to_message(&new_message);
         }
      }
   }
   return 0;
}

int
transactional_splinterdb_insert(transactional_splinterdb *txn_kvsb,
                                transaction              *txn,
                                slice                     user_key,
                                slice                     value)
{
   return local_write(
      txn_kvsb, txn, user_key, message_create(MESSAGE_TYPE_INSERT, value));
}

int
transactional_splinterdb_delete(transactional_splinterdb *txn_kvsb,
                                transaction              *txn,
                                slice                     user_key)
{
   return local_write(txn_kvsb, txn, user_key, DELETE_MESSAGE);
}

int
transactional_splinterdb_update(transactional_splinterdb *txn_kvsb,
                                transaction              *txn,
                                slice                     user_key,
                                slice                     delta)
{
   return local_write(
      txn_kvsb, txn, user_key, message_create(MESSAGE_TYPE_UPDATE, delta));
}

int
transactional_splinterdb_lookup(transactional_splinterdb *txn_kvsb,
                                transaction              *txn,
                                slice                     user_key,
                                splinterdb_lookup_result *result)
{
   const data_config *cfg   = txn_kvsb->tcfg->kvsb_cfg.data_cfg;
   rw_entry          *entry = rw_entry_get(txn_kvsb, txn, user_key, cfg, TRUE);

   int rc = 0;

   bool is_new_entry = rw_entry_iceberg_insert(txn_kvsb, entry);

   timestamp_set              v1, v2;
   _splinterdb_lookup_result *_result = (_splinterdb_lookup_result *)result;
   tuple_header              *tuple;

#if EXPERIMENTAL_MODE_BYPASS_SPLINTERDB == 0
      if (rw_entry_is_write(entry)) {
         // read my write
         // TODO This works for simple insert/update. However, it doesn't work
         // for upsert.
         // TODO if it succeeded, this read should not be considered for
         // validation. entry->is_read should be false.
         _splinterdb_lookup_result *_result =
            (_splinterdb_lookup_result *)result;
         merge_accumulator_resize(&_result->value, message_length(entry->msg));
         memcpy(merge_accumulator_data(&_result->value),
                message_data(entry->msg),
                message_length(entry->msg));
      } else {
         timestamp_set_load(entry->tuple_ts, &v1);
         entry->wts = v1.wts;
         entry->rts = timestamp_set_get_rts(&v1);
      }
      return rc;
   }

   do {
      timestamp_set_load(entry->tuple_ts, &v1);
      if (v1.lock_bit) {
         continue;
      }

      rc = splinterdb_lookup(txn_kvsb->kvsb, entry->key, result);
      platform_assert(rc == 0, "Error from SplinterDB: %d\n", rc);
      if (!splinterdb_lookup_found(result)) {
         platform_assert(0, "invalid path at this moment\n");
         return rc;
      }

      _result = (_splinterdb_lookup_result *)result;
      tuple   = (tuple_header *)merge_accumulator_data(&_result->value);

      v2       = v1;
      v2.delta = MAX(v2.delta, tuple->delta);
      v2.wts   = MAX(v2.wts, tuple->wts);

      // Why does this work?
      const uint64 value_len =
         merge_accumulator_length(&_result->value) - sizeof(tuple_header);
      memmove(tuple, tuple->value, value_len);
      merge_accumulator_resize(&_result->value, value_len);

   } while (!timestamp_set_compare_and_swap(entry->tuple_ts, &v1, &v2));

   // FIXME: Why does this code causes a segfault?
   // const uint64 value_len =
   //    merge_accumulator_length(&_result->value) - sizeof(tuple_header);
   // memmove(tuple, tuple->value, value_len);
   // merge_accumulator_resize(&_result->value, value_len);

   entry->wts = v2.wts;
   entry->rts = timestamp_set_get_rts(&v2);

   return rc;
}

void
transactional_splinterdb_lookup_result_init(
   transactional_splinterdb *txn_kvsb,   // IN
   splinterdb_lookup_result *result,     // IN/OUT
   uint64                    buffer_len, // IN
   char                     *buffer      // IN
)
{
   return splinterdb_lookup_result_init(
      txn_kvsb->kvsb, result, buffer_len, buffer);
}

void
transactional_splinterdb_set_isolation_level(
   transactional_splinterdb   *txn_kvsb,
   transaction_isolation_level isol_level)
{
   platform_assert(isol_level > TRANSACTION_ISOLATION_LEVEL_INVALID);
   platform_assert(isol_level < TRANSACTION_ISOLATION_LEVEL_MAX_VALID);

   txn_kvsb->tcfg->isol_level = isol_level;
}