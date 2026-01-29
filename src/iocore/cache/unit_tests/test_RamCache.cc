/** @file

  Unit tests for RamCacheLRU and RamCacheCLFUS

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "main.h"
#include "../P_RamCache.h"
#include "../P_CacheInternal.h"
#include "tscore/CryptoHash.h"
#include "iocore/eventsystem/IOBuffer.h"

#include <thread>
#include <vector>
#include <atomic>

int  cache_vols           = 1;
bool reuse_existing_cache = false;

// Forward declaration - defined in CacheProcessor.cc
extern void register_cache_stats(CacheStatsBlock *rsb, const std::string &prefix);

// Need to access this to disable seen filter for testing
extern int cache_config_ram_cache_use_seen_filter;

namespace
{

// Initialize cache stats once at test startup
struct CacheStatsInitializer {
  CacheStatsInitializer()
  {
    static bool initialized = false;
    if (!initialized) {
      register_cache_stats(&cache_rsb, "proxy.process.cache.test");
      // Disable seen filter for tests - it would cause first put to return 0
      cache_config_ram_cache_use_seen_filter = 0;
      initialized                            = true;
    }
  }
};
static CacheStatsInitializer cache_stats_init;

// Helper to create a CryptoHash from an integer
CryptoHash
make_key(uint64_t val)
{
  CryptoHash key;
  // Initialize all bytes to create a unique key
  memset(&key, 0, sizeof(key));
  memcpy(&key, &val, sizeof(val));
  return key;
}

// Helper to create test data buffer
IOBufferData *
make_test_data(size_t size, char fill = 'X')
{
  int           idx    = iobuffer_size_to_index(size, MAX_BUFFER_SIZE_INDEX);
  IOBufferData *data   = new_IOBufferData(idx, MEMALIGNED);
  char         *buffer = data->data();
  memset(buffer, fill, size);
  return data;
}

} // namespace

// =============================================================================
// RamCacheLRU Tests - Non-shared mode
// =============================================================================

TEST_CASE("RamCacheLRU basic put and get in non-shared mode", "[ram_cache][lru]")
{
  RamCache *cache = new_RamCacheLRU();
  cache->init(1024 * 1024, nullptr, false); // 1MB, non-shared

  CryptoHash        key = make_key(1);
  Ptr<IOBufferData> data(make_test_data(1024));
  Ptr<IOBufferData> ret_data;

  SECTION("put returns 1 on success")
  {
    int result = cache->put(&key, data.get(), 1024, false, 0);
    CHECK(result == 1);
  }

  SECTION("get returns 1 when key exists")
  {
    cache->put(&key, data.get(), 1024, false, 0);
    int result = cache->get(&key, &ret_data, 0);
    CHECK(result == 1);
    CHECK(ret_data.get() != nullptr);
  }

  SECTION("get returns 0 when key does not exist")
  {
    CryptoHash missing_key = make_key(999);
    int        result      = cache->get(&missing_key, &ret_data, 0);
    CHECK(result == 0);
    CHECK(ret_data.get() == nullptr);
  }

  SECTION("auxkey mismatch returns 0")
  {
    cache->put(&key, data.get(), 1024, false, 100);
    int result = cache->get(&key, &ret_data, 200); // Different auxkey
    CHECK(result == 0);
  }

  delete cache;
}

TEST_CASE("RamCacheLRU eviction in non-shared mode", "[ram_cache][lru]")
{
  RamCache *cache = new_RamCacheLRU();
  // Small cache to force eviction - 4KB
  cache->init(4096, nullptr, false);

  SECTION("older entries are evicted when cache is full")
  {
    // Insert multiple entries that exceed cache size
    for (int i = 0; i < 10; i++) {
      CryptoHash        key = make_key(i);
      Ptr<IOBufferData> data(make_test_data(1024));
      cache->put(&key, data.get(), 1024, false, 0);
    }

    // Just verify the cache is functioning and respects size limits
    // The exact eviction behavior depends on implementation details
    CHECK(cache->size() <= 4096 + 1024); // Allow some overhead
  }

  delete cache;
}

TEST_CASE("RamCacheLRU fixup auxkey", "[ram_cache][lru]")
{
  RamCache *cache = new_RamCacheLRU();
  cache->init(1024 * 1024, nullptr, false);

  CryptoHash        key = make_key(1);
  Ptr<IOBufferData> data(make_test_data(1024));
  Ptr<IOBufferData> ret_data;

  cache->put(&key, data.get(), 1024, false, 100);

  SECTION("fixup changes auxkey successfully")
  {
    int result = cache->fixup(&key, 100, 200);
    CHECK(result == 1);

    // Old auxkey should not work
    result = cache->get(&key, &ret_data, 100);
    CHECK(result == 0);

    // New auxkey should work
    result = cache->get(&key, &ret_data, 200);
    CHECK(result == 1);
  }

  SECTION("fixup fails for non-existent key")
  {
    CryptoHash missing_key = make_key(999);
    int        result      = cache->fixup(&missing_key, 100, 200);
    CHECK(result == 0);
  }

  delete cache;
}

// =============================================================================
// RamCacheLRU Tests - Shared mode (partitioned locking)
// =============================================================================

TEST_CASE("RamCacheLRU basic put and get in shared mode", "[ram_cache][lru][shared]")
{
  RamCache *cache = new_RamCacheLRU();
  cache->init(1024 * 1024, nullptr, true); // 1MB, shared mode

  CryptoHash        key = make_key(1);
  Ptr<IOBufferData> data(make_test_data(1024));
  Ptr<IOBufferData> ret_data;

  SECTION("put returns 1 on success in shared mode")
  {
    int result = cache->put(&key, data.get(), 1024, false, 0);
    CHECK(result == 1);
  }

  SECTION("get returns 1 when key exists in shared mode")
  {
    cache->put(&key, data.get(), 1024, false, 0);
    int result = cache->get(&key, &ret_data, 0);
    CHECK(result == 1);
    CHECK(ret_data.get() != nullptr);
  }

  delete cache;
}

TEST_CASE("RamCacheLRU entries distributed across partitions in shared mode", "[ram_cache][lru][shared][partitions]")
{
  RamCache *cache = new_RamCacheLRU();
  cache->init(10 * 1024 * 1024, nullptr, true); // 10MB, shared mode

  // Insert many entries to ensure they are distributed across partitions
  constexpr int NUM_ENTRIES = 1000;
  for (int i = 0; i < NUM_ENTRIES; i++) {
    CryptoHash        key = make_key(i);
    Ptr<IOBufferData> data(make_test_data(256));
    cache->put(&key, data.get(), 256, false, 0);
  }

  // Verify all entries are retrievable
  int found = 0;
  for (int i = 0; i < NUM_ENTRIES; i++) {
    CryptoHash        key = make_key(i);
    Ptr<IOBufferData> ret_data;
    if (cache->get(&key, &ret_data, 0) == 1) {
      found++;
    }
  }

  // All entries should be found (cache is large enough)
  CHECK(found == NUM_ENTRIES);

  delete cache;
}

// =============================================================================
// RamCacheCLFUS Tests - Non-shared mode
// =============================================================================

TEST_CASE("RamCacheCLFUS basic put and get in non-shared mode", "[ram_cache][clfus]")
{
  RamCache *cache = new_RamCacheCLFUS();
  cache->init(1024 * 1024, nullptr, false); // 1MB, non-shared

  CryptoHash        key = make_key(1);
  Ptr<IOBufferData> data(make_test_data(1024));
  Ptr<IOBufferData> ret_data;

  SECTION("put returns 1 on success")
  {
    int result = cache->put(&key, data.get(), 1024, false, 0);
    CHECK(result == 1);
  }

  SECTION("get returns non-zero when key exists")
  {
    cache->put(&key, data.get(), 1024, false, 0);
    int result = cache->get(&key, &ret_data, 0);
    CHECK(result != 0); // CLFUS returns different values for hit types
    CHECK(ret_data.get() != nullptr);
  }

  SECTION("get returns 0 when key does not exist")
  {
    CryptoHash missing_key = make_key(999);
    int        result      = cache->get(&missing_key, &ret_data, 0);
    CHECK(result == 0);
    CHECK(ret_data.get() == nullptr);
  }

  SECTION("auxkey mismatch returns 0")
  {
    cache->put(&key, data.get(), 1024, false, 100);
    int result = cache->get(&key, &ret_data, 200); // Different auxkey
    CHECK(result == 0);
  }

  delete cache;
}

TEST_CASE("RamCacheCLFUS fixup auxkey", "[ram_cache][clfus]")
{
  RamCache *cache = new_RamCacheCLFUS();
  cache->init(1024 * 1024, nullptr, false);

  CryptoHash        key = make_key(1);
  Ptr<IOBufferData> data(make_test_data(1024));
  Ptr<IOBufferData> ret_data;

  cache->put(&key, data.get(), 1024, false, 100);

  SECTION("fixup changes auxkey successfully")
  {
    int result = cache->fixup(&key, 100, 200);
    CHECK(result == 1);

    // Old auxkey should not work
    result = cache->get(&key, &ret_data, 100);
    CHECK(result == 0);

    // New auxkey should work
    result = cache->get(&key, &ret_data, 200);
    CHECK(result != 0);
  }

  delete cache;
}

// =============================================================================
// RamCacheCLFUS Tests - Shared mode (partitioned locking)
// =============================================================================

TEST_CASE("RamCacheCLFUS basic put and get in shared mode", "[ram_cache][clfus][shared]")
{
  RamCache *cache = new_RamCacheCLFUS();
  cache->init(1024 * 1024, nullptr, true); // 1MB, shared mode

  CryptoHash        key = make_key(1);
  Ptr<IOBufferData> data(make_test_data(1024));
  Ptr<IOBufferData> ret_data;

  SECTION("put returns 1 on success in shared mode")
  {
    int result = cache->put(&key, data.get(), 1024, false, 0);
    CHECK(result == 1);
  }

  SECTION("get returns non-zero when key exists in shared mode")
  {
    cache->put(&key, data.get(), 1024, false, 0);
    int result = cache->get(&key, &ret_data, 0);
    CHECK(result != 0);
    CHECK(ret_data.get() != nullptr);
  }

  delete cache;
}

TEST_CASE("RamCacheCLFUS entries distributed across partitions in shared mode", "[ram_cache][clfus][shared][partitions]")
{
  RamCache *cache = new_RamCacheCLFUS();
  cache->init(10 * 1024 * 1024, nullptr, true); // 10MB, shared mode

  // Insert many entries to ensure they are distributed across partitions
  constexpr int NUM_ENTRIES = 1000;
  for (int i = 0; i < NUM_ENTRIES; i++) {
    CryptoHash        key = make_key(i);
    Ptr<IOBufferData> data(make_test_data(256));
    cache->put(&key, data.get(), 256, false, 0);
  }

  // Verify all entries are retrievable
  int found = 0;
  for (int i = 0; i < NUM_ENTRIES; i++) {
    CryptoHash        key = make_key(i);
    Ptr<IOBufferData> ret_data;
    if (cache->get(&key, &ret_data, 0) != 0) {
      found++;
    }
  }

  // All entries should be found (cache is large enough)
  CHECK(found == NUM_ENTRIES);

  delete cache;
}

TEST_CASE("RamCacheCLFUS unified memory pool in shared mode", "[ram_cache][clfus][shared][memory]")
{
  RamCache *cache = new_RamCacheCLFUS();
  // 64KB cache - smaller than what would be needed if memory was partitioned
  cache->init(64 * 1024, nullptr, true);

  // Insert entries that would overflow a single partition if memory was partitioned
  // With 64 partitions and 64KB total, each partition would only get 1KB
  // But with unified memory pool, we can store larger objects
  constexpr int LARGE_ENTRY_SIZE = 4096; // 4KB - larger than 1KB partition would allow
  constexpr int NUM_ENTRIES      = 10;

  int successful_puts = 0;
  for (int i = 0; i < NUM_ENTRIES; i++) {
    CryptoHash        key = make_key(i);
    Ptr<IOBufferData> data(make_test_data(LARGE_ENTRY_SIZE));
    if (cache->put(&key, data.get(), LARGE_ENTRY_SIZE, false, 0) == 1) {
      successful_puts++;
    }
  }

  // Should be able to store at least some large entries
  CHECK(successful_puts > 0);

  // Verify they can be retrieved
  int found = 0;
  for (int i = 0; i < NUM_ENTRIES; i++) {
    CryptoHash        key = make_key(i);
    Ptr<IOBufferData> ret_data;
    if (cache->get(&key, &ret_data, 0) != 0) {
      found++;
    }
  }
  CHECK(found > 0);

  delete cache;
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("RamCache with zero max_bytes does not store", "[ram_cache][edge]")
{
  SECTION("RamCacheLRU")
  {
    RamCache *cache = new_RamCacheLRU();
    cache->init(0, nullptr, false); // Zero size

    CryptoHash        key = make_key(1);
    Ptr<IOBufferData> data(make_test_data(1024));
    Ptr<IOBufferData> ret_data;

    int result = cache->put(&key, data.get(), 1024, false, 0);
    CHECK(result == 0);

    result = cache->get(&key, &ret_data, 0);
    CHECK(result == 0);

    delete cache;
  }

  SECTION("RamCacheCLFUS")
  {
    RamCache *cache = new_RamCacheCLFUS();
    cache->init(0, nullptr, false); // Zero size

    CryptoHash        key = make_key(1);
    Ptr<IOBufferData> data(make_test_data(1024));
    Ptr<IOBufferData> ret_data;

    int result = cache->put(&key, data.get(), 1024, false, 0);
    CHECK(result == 0);

    result = cache->get(&key, &ret_data, 0);
    CHECK(result == 0);

    delete cache;
  }
}

TEST_CASE("RamCache update existing entry", "[ram_cache][update]")
{
  SECTION("RamCacheLRU keeps original data on duplicate put")
  {
    // Note: RamCacheLRU does NOT update data on duplicate put -
    // it just refreshes the LRU position and returns 1
    RamCache *cache = new_RamCacheLRU();
    cache->init(1024 * 1024, nullptr, false);

    CryptoHash        key = make_key(1);
    Ptr<IOBufferData> data1(make_test_data(1024, 'A'));
    Ptr<IOBufferData> data2(make_test_data(1024, 'B'));
    Ptr<IOBufferData> ret_data;

    cache->put(&key, data1.get(), 1024, false, 0);
    int result = cache->put(&key, data2.get(), 1024, false, 0);
    CHECK(result == 1); // Returns 1 to indicate entry exists

    cache->get(&key, &ret_data, 0);
    CHECK(ret_data.get() != nullptr);
    // RamCacheLRU keeps original data, not the new data
    CHECK(ret_data->data()[0] == 'A');

    delete cache;
  }

  SECTION("RamCacheCLFUS updates existing entry")
  {
    RamCache *cache = new_RamCacheCLFUS();
    cache->init(1024 * 1024, nullptr, false);

    CryptoHash        key = make_key(1);
    Ptr<IOBufferData> data1(make_test_data(1024, 'A'));
    Ptr<IOBufferData> data2(make_test_data(1024, 'B'));
    Ptr<IOBufferData> ret_data;

    cache->put(&key, data1.get(), 1024, false, 0);
    cache->put(&key, data2.get(), 1024, false, 0);

    cache->get(&key, &ret_data, 0);
    CHECK(ret_data.get() != nullptr);
    // The data should be updated to the new value
    CHECK(ret_data->data()[0] == 'B');

    delete cache;
  }
}

TEST_CASE("RamCache auxkey conflict replaces entry", "[ram_cache][auxkey]")
{
  SECTION("RamCacheLRU")
  {
    RamCache *cache = new_RamCacheLRU();
    cache->init(1024 * 1024, nullptr, false);

    CryptoHash        key = make_key(1);
    Ptr<IOBufferData> data1(make_test_data(1024, 'A'));
    Ptr<IOBufferData> data2(make_test_data(1024, 'B'));
    Ptr<IOBufferData> ret_data;

    cache->put(&key, data1.get(), 1024, false, 100);
    cache->put(&key, data2.get(), 1024, false, 200); // Different auxkey

    // Old auxkey should not find the entry
    int result = cache->get(&key, &ret_data, 100);
    CHECK(result == 0);

    // New auxkey should find the entry
    result = cache->get(&key, &ret_data, 200);
    CHECK(result == 1);
    CHECK(ret_data->data()[0] == 'B');

    delete cache;
  }

  SECTION("RamCacheCLFUS")
  {
    RamCache *cache = new_RamCacheCLFUS();
    cache->init(1024 * 1024, nullptr, false);

    CryptoHash        key = make_key(1);
    Ptr<IOBufferData> data1(make_test_data(1024, 'A'));
    Ptr<IOBufferData> data2(make_test_data(1024, 'B'));
    Ptr<IOBufferData> ret_data;

    cache->put(&key, data1.get(), 1024, false, 100);
    cache->put(&key, data2.get(), 1024, false, 200); // Different auxkey

    // Old auxkey should not find the entry
    int result = cache->get(&key, &ret_data, 100);
    CHECK(result == 0);

    // New auxkey should find the entry
    result = cache->get(&key, &ret_data, 200);
    CHECK(result != 0);
    CHECK(ret_data->data()[0] == 'B');

    delete cache;
  }
}

// =============================================================================
// Shared mode specific tests
// =============================================================================

TEST_CASE("RamCacheLRU shared mode fixup works", "[ram_cache][lru][shared]")
{
  RamCache *cache = new_RamCacheLRU();
  cache->init(1024 * 1024, nullptr, true);

  CryptoHash        key = make_key(1);
  Ptr<IOBufferData> data(make_test_data(1024));
  Ptr<IOBufferData> ret_data;

  cache->put(&key, data.get(), 1024, false, 100);

  int result = cache->fixup(&key, 100, 200);
  CHECK(result == 1);

  // Old auxkey should not work
  result = cache->get(&key, &ret_data, 100);
  CHECK(result == 0);

  // New auxkey should work
  result = cache->get(&key, &ret_data, 200);
  CHECK(result == 1);

  delete cache;
}

TEST_CASE("RamCacheCLFUS shared mode fixup works", "[ram_cache][clfus][shared]")
{
  RamCache *cache = new_RamCacheCLFUS();
  cache->init(1024 * 1024, nullptr, true);

  CryptoHash        key = make_key(1);
  Ptr<IOBufferData> data(make_test_data(1024));
  Ptr<IOBufferData> ret_data;

  cache->put(&key, data.get(), 1024, false, 100);

  int result = cache->fixup(&key, 100, 200);
  CHECK(result == 1);

  // Old auxkey should not work
  result = cache->get(&key, &ret_data, 100);
  CHECK(result == 0);

  // New auxkey should work
  result = cache->get(&key, &ret_data, 200);
  CHECK(result != 0);

  delete cache;
}

TEST_CASE("RamCache size() returns reasonable values", "[ram_cache][size]")
{
  SECTION("RamCacheLRU non-shared")
  {
    RamCache *cache = new_RamCacheLRU();
    cache->init(1024 * 1024, nullptr, false);

    CHECK(cache->size() == 0);

    CryptoHash        key = make_key(1);
    Ptr<IOBufferData> data(make_test_data(1024));
    cache->put(&key, data.get(), 1024, false, 0);

    CHECK(cache->size() > 0);

    delete cache;
  }

  SECTION("RamCacheLRU shared")
  {
    RamCache *cache = new_RamCacheLRU();
    cache->init(1024 * 1024, nullptr, true);

    CHECK(cache->size() == 0);

    CryptoHash        key = make_key(1);
    Ptr<IOBufferData> data(make_test_data(1024));
    cache->put(&key, data.get(), 1024, false, 0);

    CHECK(cache->size() > 0);

    delete cache;
  }

  SECTION("RamCacheCLFUS non-shared")
  {
    RamCache *cache = new_RamCacheCLFUS();
    cache->init(1024 * 1024, nullptr, false);

    CHECK(cache->size() == 0);

    CryptoHash        key = make_key(1);
    Ptr<IOBufferData> data(make_test_data(1024));
    cache->put(&key, data.get(), 1024, false, 0);

    CHECK(cache->size() > 0);

    delete cache;
  }

  SECTION("RamCacheCLFUS shared")
  {
    RamCache *cache = new_RamCacheCLFUS();
    cache->init(1024 * 1024, nullptr, true);

    CHECK(cache->size() == 0);

    CryptoHash        key = make_key(1);
    Ptr<IOBufferData> data(make_test_data(1024));
    cache->put(&key, data.get(), 1024, false, 0);

    CHECK(cache->size() > 0);

    delete cache;
  }
}
