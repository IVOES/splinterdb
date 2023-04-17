/*
** ****************************************************************************
** loc_tokens.h
**
** WARNING: this is a generated file. Any change you make here will be overwritten!
**
** This file was generated by processing all source files under
**     /Users/agurajada/Code/splinterdb/
**
** Script executed: /Users/agurajada/Projects/LOC-Take2/./gen_filenames_defines.py
** ****************************************************************************
*/

//clang-format off
#ifndef __LOC_TOKENS_H__

// LOC_MAX_FILE_NUM=60   ... Used to update generated file in the source tree.

#define LOC_UNKNOWN_FILE                                  0     // Unknown_file: L=0 (line count)
#define LOC_PackedArray_c                                 1     // src/PackedArray.c: L=561
#define LOC_allocator_c                                   2     // src/allocator.c: L=27
#define LOC_avlTree_c                                     3     // tests/functional/avlTree.c: L=931
#define LOC_btree_c                                       4     // src/btree.c: L=3624
#define LOC_btree_stress_test_c                           5     // tests/unit/btree_stress_test.c: L=546
#define LOC_btree_test_c                                  6     // tests/functional/btree_test.c: L=1720
#define LOC_btree_test_common_c                           7     // tests/unit/btree_test_common.c: L=76
#define LOC_cache_test_c                                  8     // tests/functional/cache_test.c: L=1167
#define LOC_clockcache_c                                  9     // src/clockcache.c: L=3276
#define LOC_config_c                                      10    // tests/config.c: L=435
#define LOC_config_parse_test_c                           11    // tests/unit/config_parse_test.c: L=135
#define LOC_data_internal_c                               12    // src/data_internal.c: L=53
#define LOC_default_data_config_c                         13    // src/default_data_config.c: L=92
#define LOC_driver_test_c                                 14    // tests/functional/driver_test.c: L=14
#define LOC_filter_test_c                                 15    // tests/functional/filter_test.c: L=464
#define LOC_io_apis_test_c                                16    // tests/functional/io_apis_test.c: L=1059
#define LOC_laio_c                                        17    // src/platform_linux/laio.c: L=654
#define LOC_large_inserts_stress_test_c                   18    // tests/unit/large_inserts_stress_test.c: L=943
#define LOC_limitations_test_c                            19    // tests/unit/limitations_test.c: L=551
#define LOC_loc_filenames_c                               20    // src/loc_filenames.c: L=85
#define LOC_log_test_c                                    21    // tests/functional/log_test.c: L=386
#define LOC_main_c                                        22    // tests/unit/main.c: L=1057
#define LOC_memtable_c                                    23    // src/memtable.c: L=354
#define LOC_merge_c                                       24    // src/merge.c: L=663
#define LOC_mini_allocator_c                              25    // src/mini_allocator.c: L=1376
#define LOC_misc_test_c                                   26    // tests/unit/misc_test.c: L=323
#define LOC_platform_c                                    27    // src/platform_linux/platform.c: L=542
#define LOC_platform_apis_test_c                          28    // tests/unit/platform_apis_test.c: L=321
#define LOC_rc_allocator_c                                29    // src/rc_allocator.c: L=880
#define LOC_routing_filter_c                              30    // src/routing_filter.c: L=1371
#define LOC_shard_log_c                                   31    // src/shard_log.c: L=526
#define LOC_shmem_c                                       32    // src/platform_linux/shmem.c: L=1817
#define LOC_splinter_ipc_test_c                           33    // tests/unit/splinter_ipc_test.c: L=32
#define LOC_splinter_shmem_oom_test_c                     34    // tests/unit/splinter_shmem_oom_test.c: L=314
#define LOC_splinter_shmem_test_c                         35    // tests/unit/splinter_shmem_test.c: L=1178
#define LOC_splinter_test_c                               36    // tests/functional/splinter_test.c: L=3055
#define LOC_splinterdb_c                                  37    // src/splinterdb.c: L=848
#define LOC_splinterdb_custom_ipv4_addr_sortcmp_example_c 38    // examples/splinterdb_custom_ipv4_addr_sortcmp_example.c: L=385
#define LOC_splinterdb_forked_child_test_c                39    // tests/unit/splinterdb_forked_child_test.c: L=663
#define LOC_splinterdb_intro_example_c                    40    // examples/splinterdb_intro_example.c: L=137
#define LOC_splinterdb_iterators_example_c                41    // examples/splinterdb_iterators_example.c: L=168
#define LOC_splinterdb_quick_test_c                       42    // tests/unit/splinterdb_quick_test.c: L=1039
#define LOC_splinterdb_stress_test_c                      43    // tests/unit/splinterdb_stress_test.c: L=280
#define LOC_splinterdb_wide_values_example_c              44    // examples/splinterdb_wide_values_example.c: L=115
#define LOC_task_c                                        45    // src/task.c: L=1055
#define LOC_task_system_test_c                            46    // tests/unit/task_system_test.c: L=852
#define LOC_test_async_c                                  47    // tests/functional/test_async.c: L=188
#define LOC_test_common_c                                 48    // tests/test_common.c: L=158
#define LOC_test_data_c                                   49    // tests/test_data.c: L=139
#define LOC_test_dispatcher_c                             50    // tests/functional/test_dispatcher.c: L=66
#define LOC_test_functionality_c                          51    // tests/functional/test_functionality.c: L=878
#define LOC_test_splinter_shadow_c                        52    // tests/functional/test_splinter_shadow.c: L=344
#define LOC_trunk_c                                       53    // src/trunk.c: L=9331
#define LOC_unit_btree_test_c                             54    // tests/unit/btree_test.c: L=492
#define LOC_unit_splinter_test_c                          55    // tests/unit/splinter_test.c: L=922
#define LOC_unit_tests_common_c                           56    // tests/unit/unit_tests_common.c: L=76
#define LOC_util_c                                        57    // src/util.c: L=436
#define LOC_util_test_c                                   58    // tests/unit/util_test.c: L=116
#define LOC_writable_buffer_test_c                        59    // tests/unit/writable_buffer_test.c: L=415
#define LOC_ycsb_test_c                                   60    // tests/functional/ycsb_test.c: L=1387

#define LOC_MAX_FILE_NUM                                  60    // LOC MAX LINE COUNT: L=9331
#define LOC_NUM_FILES                                     61    // Size of filenames lookup array: L=0

//clang-format off
#endif  /* __LOC_TOKENS_H__ */
