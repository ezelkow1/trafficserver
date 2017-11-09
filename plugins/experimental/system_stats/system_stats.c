/** @file

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

  #include "ts/ink_config.h"
  #include "ts/ink_defs.h"

  #include "ts/ts.h"
  #include <stdint.h>
  #include <stdbool.h>
  #include <string.h>
  #include <stdio.h>
  #include <getopt.h>
  #include <search.h>
  #include <inttypes.h>
  #include <stdlib.h>
  #include <sys/types.h>
  #include <dirent.h>

  #include <unistd.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <ftw.h>

  #define PLUGIN_NAME "system_stats"
  #define DEBUG_TAG PLUGIN_NAME

  /* Stat string names */
  #define LOAD_AVG_ONE_MIN "plugin." PLUGIN_NAME ".loadavg.one"
  #define LOAD_AVG_FIVE_MIN "plugin." PLUGIN_NAME ".loadavg.five"
  #define LOAD_AVG_TEN_MIN "plugin." PLUGIN_NAME ".loadavg.ten"

  /* 
   * Base net stats name, full name needs to populated
   * with NET_STATS.infname.RX/TX.standard_net_stats field
   * */
  #define NET_STATS "plugin." PLUGIN_NAME ".net."

  /* pre-defined record types for indexing to the hash */
  #define SPEED "speed"
  #define INTERFACE "interface"
  #define RECORD_TYPES "record_types"
  #define NET_DEV "net_dev"
  #define LOAD_AVG "load_avg"

  typedef struct 
  {
    TSStatPersistence persist_type;
    TSMutex stat_creation_mutex;
  } config_t;

  typedef struct
  {
    int bytes;
    int packets;
    int errs;
    int drop;
    int fifo;
    int compressed;
  } standard_net_stats;

  typedef struct
  {
    char *interfaceName;
    standard_net_stats rx;
    standard_net_stats tx;
    int collisions;
    int multicast;
    int speed;
  } sys_net_stats;

  typedef struct
  {
    int one_minute;
    int five_minute;
    int ten_minute;
    int   running_processes;
    int   total_processes;
    int   last_pid;
  } load_avg;

  typedef struct 
  {
    sys_net_stats net_stats;
    load_avg      load_stats;
    TSMutex stat_creation_mutex;
  } stats_state;

  int configReloadRequests = 0;
  int configReloads = 0;
  time_t lastReloadRequest = 0;
  time_t lastReload = 0;
  time_t astatsLoad = 0;

  static int
  stat_add(char *name, TSRecordDataType record_type, TSMutex create_mutex)
  {
    int stat_id = -1;

    TSMutexLock(create_mutex);
    if (TS_ERROR == TSStatFindName((const char *)name, &stat_id)) 
    {
      stat_id = TSStatCreate((const char *)name, record_type, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (stat_id == TS_ERROR) 
      {
        TSDebug(DEBUG_TAG, "Error creating stat_name: %s", name);
      } 
      else 
      {
        TSDebug(DEBUG_TAG, "Created stat_name: %s stat_id: %d", name, stat_id);
      }
    }
    TSMutexUnlock(create_mutex);
    return stat_id;
  }

  static char * getFile(char *filename, char *buffer, int bufferSize) 
  {
    TSFile f= 0;
    size_t s = 0;

    f = TSfopen(filename, "r");
    if (!f)
    {
      buffer[0] = 0;
      return buffer;
    }

    s = TSfread(f, buffer, bufferSize);
    if (s > 0)
      buffer[s] = 0;
    else
      buffer[0] = 0;

    TSfclose(f);

    return buffer;
  }

  static void stat_set(char *name, int value, TSMutex stat_creation_mutex)
  {
    int stat_id = stat_add(name, TS_RECORDDATATYPE_INT, stat_creation_mutex);
    TSStatIntSet(stat_id, value);
  }

  static int net_stats_info(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
  {
    TSDebug(DEBUG_TAG, "path: %s, level: %d, base: %d", fpath, ftwbuf->level, ftwbuf->base);
    return 0;
  }

  static void get_stats(stats_state *my_state)
  {
    double loadavg[3] = {0,0,0};
    getloadavg(loadavg, 3);

    /* Convert the doubles to int */
    my_state->load_stats.one_minute = loadavg[0]*100;
    my_state->load_stats.five_minute = loadavg[1]*100;
    my_state->load_stats.ten_minute = loadavg[2]*100;

    stat_set(LOAD_AVG_ONE_MIN, my_state->load_stats.one_minute, my_state->stat_creation_mutex);
    stat_set(LOAD_AVG_FIVE_MIN, my_state->load_stats.five_minute, my_state->stat_creation_mutex);
    stat_set(LOAD_AVG_TEN_MIN, my_state->load_stats.ten_minute, my_state->stat_creation_mutex);

    ntfw("/sys/class/net", net_stats_info, 10, 0);
    return;
  }

  static int handle_read_req_hdr(TSCont cont, TSEvent event ATS_UNUSED, void *edata)
  {
    TSHttpTxn txn = (TSHttpTxn)edata;
    config_t *config;
    TSEvent reenable = TS_EVENT_HTTP_CONTINUE;
    stats_state *my_state;

    config = (config_t *)TSContDataGet(cont);

    my_state = (stats_state *) TSmalloc(sizeof(*my_state));
    memset(my_state, 0, sizeof(*my_state));
    my_state->stat_creation_mutex = config->stat_creation_mutex;
    get_stats(my_state);

    TSDebug(DEBUG_TAG, "Read Req Handler Finished");
    TSHttpTxnReenable(txn, reenable);
    return 0;
  }

  void TSPluginInit(int argc, const char *argv[])
  {
    TSPluginRegistrationInfo info;
    TSCont stats_cont;
    //TSCont pre_remap_cont, post_remap_cont, global_cont;
    config_t *config;
  
    info.plugin_name   = PLUGIN_NAME;
    info.vendor_name   = "Apache Software Foundation";
    info.support_email = "dev@trafficserver.apache.org";
  
    if (TSPluginRegister(&info) != TS_SUCCESS) {
      TSError("[system_stats] Plugin registration failed");
      return;
    } else {
      TSDebug(DEBUG_TAG, "Plugin registration succeeded");
    }
  
    config                      = (config_t *)TSmalloc(sizeof(config_t));
    config->persist_type        = TS_STAT_NON_PERSISTENT;
    config->stat_creation_mutex = TSMutexCreate();

    if (argc > 1) {
        //config options if necessary
    }
  
    #if 0
        TSHttpArgIndexReserve(PLUGIN_NAME, "txn data", &(config->txn_slot));
      
        global_cont = TSContCreate(handle_txn_close, NULL);
        TSContDataSet(global_cont, (void *)config);
        TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, global_cont);
    #endif  
    stats_cont = TSContCreate(handle_read_req_hdr, NULL);
    TSContDataSet(stats_cont, (void *)config);
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, stats_cont);
    /****
     * Do an initial poll here of all stats to save
     * an initial state
     * ****/
      //get_stats();
        TSDebug(DEBUG_TAG, "Init complete");
  }
  