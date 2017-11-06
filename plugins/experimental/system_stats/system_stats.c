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


  #define PLUGIN_NAME "system_stats"
  #define DEBUG_TAG PLUGIN_NAME

  /* pre-defined record types for indexing to the hash */
  #define SPEED "speed"
  #define INTERFACE "interface"
  #define RECORD_TYPES "record_types"
  #define NET_DEV "net_dev"
  #define LOAD_AVG "load_avg"

  typedef struct 
  {
    unsigned int recordTypes;
    int txn_slot;
    TSStatPersistence persist_type;
    TSMutex stat_creation_mutex;
  } config_t;

  typedef struct 
  {
    TSHttpTxn txn;

    int globals_cnt;
    char **globals;
    char *interfaceName;
    char *dev;
    char *load;
    char *query;
    int speed;
    unsigned int recordTypes;
  } stats_state;
  
  int configReloadRequests = 0;
  int configReloads = 0;
  time_t lastReloadRequest = 0;
  time_t lastReload = 0;
  time_t astatsLoad = 0;
  #define SYSTEM_RECORD_TYPE 		(0x100)
  #define DEFAULT_RECORD_TYPES	(SYSTEM_RECORD_TYPE | TS_RECORDTYPE_PROCESS | TS_RECORDTYPE_PLUGIN)


    #if 0
  static char * nstr(const char *s) 
  {
    char *mys = (char *)TSmalloc(strlen(s)+1);
    strcpy(mys, s);
    return mys;
  }
  #endif

  static char * nstrl(const char *s, int len) 
  {
    char *mys = (char *)TSmalloc(len + 1);
    memcpy(mys, s, len);
    mys[len] = 0;
    return mys;
  }

  static void
  stat_add(char *name, TSRecordDataType record_type, TSMutex create_mutex)
  {
    int stat_id = -1;
    ENTRY search, *result = NULL;
    static __thread bool hash_init = false;
  
    if (unlikely(!hash_init)) {
      hcreate(TS_MAX_API_STATS << 1);
      hash_init = true;
      TSDebug(DEBUG_TAG, "stat cache hash init");
    }
  
    search.key  = name;
    search.data = 0;
    result = hsearch(search, FIND);
  
    if (unlikely(result == NULL)) {
      // This is an unlikely path because we most likely have the stat cached
      // so this mutex won't be much overhead and it fixes a race condition
      // in the RecCore. Hopefully this can be removed in the future.
      TSMutexLock(create_mutex);
      if (TS_ERROR == TSStatFindName((const char *)name, &stat_id)) {
        stat_id = TSStatCreate((const char *)name, record_type, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
        if (stat_id == TS_ERROR) {
          TSDebug(DEBUG_TAG, "Error creating stat_name: %s", name);
        } else {
          TSDebug(DEBUG_TAG, "Created stat_name: %s stat_id: %d", name, stat_id);
        }
      }
      TSMutexUnlock(create_mutex);
  
      if (stat_id >= 0) {
        search.key  = TSstrdup(name);
        search.data = (void *)((intptr_t)stat_id);
        result = hsearch(search, ENTER);
        TSDebug(DEBUG_TAG, "Cached stat_name: %s stat_id: %d", name, stat_id);
      }
    } else {
      stat_id = (int)((intptr_t)result->data);
    }
  }

  static char ** parseGlobals(char *str, int *globals_cnt) 
  {
    char *tok = 0;
    char **globals = 0;
    char **old = 0;
    int globals_size = 0, cnt = 0, i;
  
    while (1) {
      tok = strtok_r(str, ";", &str);
      if (!tok)
        break;
      if (cnt >= globals_size) {
        old = globals;
        globals = (char **) TSmalloc(sizeof(char *) * (globals_size + 20));
        if (old) {
          memcpy(globals, old, sizeof(char *) * (globals_size));
          TSfree(old);
          old = NULL;
        }
        globals_size += 20;
      }
      globals[cnt] = tok;
      cnt++;
    }
    *globals_cnt = cnt;
  
    for (i = 0; i < cnt; i++)
      TSDebug(PLUGIN_NAME, "globals[%d]: '%s'", i, globals[i]);
  
    return globals;
  }

  static void stats_fillState(stats_state *my_state, char *query, int query_len) 
  {
    char* arg = 0;

    while (1) {
      arg = strtok_r(query, "&", &query);
      if (!arg)
        break;
      if (strstr(arg, "application=")) {
        arg = arg + strlen("application=");
        my_state->globals = parseGlobals(arg, &my_state->globals_cnt);
      } else if (strstr(arg, "inf.name=")) {
        my_state->interfaceName = arg + strlen("inf.name=");
      } else if(strstr(arg, "record.types=")) {
        my_state->recordTypes = strtol(arg + strlen("record.types="), NULL, 16);
      }
    }
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

  static int getSpeed(char *inf) 
  {
    char* str;
    char b[256];
    int speed = 0;
    char buffer[2024];
    int bsize = 2024;

    snprintf(b, sizeof(b), "/sys/class/net/%s/operstate", inf);
    str = getFile(b, buffer, bsize);
    if (str && strstr(str, "up"))
    {
      snprintf(b, sizeof(b), "/sys/class/net/%s/speed", inf);
      str = getFile(b, buffer, bsize);
      speed = strtol(str, 0, 10);
    }

    return speed;
  }

  static char * get_query(TSHttpTxn txn)
  {
    TSMBuffer reqp;
    TSMLoc hdr_loc = NULL, url_loc = NULL;
    TSHttpTxnClientReqGet(txn, &reqp, &hdr_loc);
    TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc);
    int query_len;
    char *query_unterminated = (char*)TSUrlHttpQueryGet(reqp,url_loc,&query_len);
    char *query = nstrl(query_unterminated, query_len);
    return query;
  }

  static void get_stats(stats_state *my_state)
  {
    char buffer[2024];
    int bsize = 2024;
    char *str;
    char *end;
    char *query;

    query = get_query(my_state->txn);
    stats_fillState(my_state, query, strlen(query));

    //add interface name
    //APPEND_STAT("inf.name", "\"%s\"", my_state->interfaceName);

    //add inf speed
    my_state->speed = getSpeed(my_state->interfaceName);    
    //APPEND_STAT("inf.speed", "%d", my_state->speed);

    str = getFile("/proc/net/dev", buffer, bsize);
    if (str && my_state->interfaceName) {
      str = strstr(str, my_state->interfaceName);
      if (str) {
        end = strstr(str, "\n");
        if (end)
          *end = 0;
        //APPEND_STAT("proc.net.dev", "\"%s\"", str);
        my_state->dev = str;
      }
    }
    
    str = getFile("/proc/loadavg", buffer, bsize);
    if (str) {
      end = strstr(str, "\n");
      if (end)
        *end = 0;
      //APPEND_STAT("proc.loadavg", "\"%s\"", str);
      my_state->load = str;
    }

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
  
    my_state->recordTypes = config->recordTypes;
    my_state->txn = txn;
    get_stats(my_state);
    TSDebug(DEBUG_TAG, "%s(): inf %s", __FUNCTION__, my_state->interfaceName );
    TSDebug(DEBUG_TAG, "%s(): speed: %d", __FUNCTION__, my_state->speed);
    
    TSDebug(DEBUG_TAG, "Read Req Handler Finished");
    TSHttpTxnReenable(txn, reenable);
    return 0;
  }

  static int init_stats(config_t *config)
  {
    stat_add(INTERFACE, TS_RECORDDATATYPE_STRING, config->stat_creation_mutex);
    stat_add(SPEED, TS_RECORDDATATYPE_INT, config->stat_creation_mutex);
    stat_add(RECORD_TYPES, TS_RECORDDATATYPE_INT, config->stat_creation_mutex);
    stat_add(NET_DEV, TS_RECORDDATATYPE_STRING, config->stat_creation_mutex);
    stat_add(LOAD_AVG, TS_RECORDDATATYPE_STRING, config->stat_creation_mutex);

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
  
    init_stats(config);

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
  