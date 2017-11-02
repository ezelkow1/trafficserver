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

#define PLUGIN_NAME "system_stats"
#define DEBUG_TAG PLUGIN_NAME


  typedef struct {
    int txn_slot;
    TSStatPersistence persist_type;
    TSMutex stat_creation_mutex;
  } config_t;

  typedef struct {
    int globals_cnt;
    char **globals;
    char *interfaceName;
    char *query;
    unsigned int recordTypes;
  } stats_state;

  #define APPEND(a) my_state->output_bytes += stats_add_data_to_resp_buffer(a, my_state)
  #define APPEND_STAT(a, fmt, v) do { \
      char b[3048]; \
      if (snprintf(b, sizeof(b), "   \"%s\": " fmt ",\n", a, v) < sizeof(b)) \
      APPEND(b); \
  } while(0)

  static char ** 
  parseGlobals(char *str, int *globals_cnt) 
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

  static void 
  stats_fillState(stats_state *my_state, char *query, int query_len) 
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

  static char * 
  getFile(char *filename, char *buffer, int bufferSize) 
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

  static int 
  getSpeed(char *inf, char *buffer, int bufferSize) 
  {
    char* str;
    char b[256];
    int speed = 0;

    snprintf(b, sizeof(b), "/sys/class/net/%s/operstate", inf);
    str = getFile(b, buffer, bufferSize);
    if (str && strstr(str, "up"))
    {
      snprintf(b, sizeof(b), "/sys/class/net/%s/speed", inf);
      str = getFile(b, buffer, bufferSize);
      speed = strtol(str, 0, 10);
    }

    return speed;
  }

  static char *
  get_effective_host(TSHttpTxn txn)
  {
    char *effective_url, *tmp;
    const char *host;
    int len;
    TSMBuffer buf;
    TSMLoc url_loc;
  
    buf = TSMBufferCreate();
    if (TS_SUCCESS != TSUrlCreate(buf, &url_loc)) {
      TSDebug(DEBUG_TAG, "unable to create url");
      TSMBufferDestroy(buf);
      return NULL;
    }
    tmp = effective_url = TSHttpTxnEffectiveUrlStringGet(txn, &len);
    TSUrlParse(buf, url_loc, (const char **)(&tmp), (const char *)(effective_url + len));
    TSfree(effective_url);
    host = TSUrlHostGet(buf, url_loc, &len);
    tmp  = TSstrndup(host, len);
    TSHandleMLocRelease(buf, TS_NULL_MLOC, url_loc);
    TSMBufferDestroy(buf);
    return tmp;
  }

  static int
  handle_read_req_hdr(TSCont cont, TSEvent event ATS_UNUSED, void *edata)
  {
    TSHttpTxn txn = (TSHttpTxn)edata;
    config_t *config;
    void *txnd;

    config = (config_t *)TSContDataGet(cont);
    txnd   = (void *)get_effective_host(txn); // low bit left 0 because we do not know that remap succeeded yet
    TSHttpTxnArgSet(txn, config->txn_slot, txnd);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    TSDebug(DEBUG_TAG, "Read Req Handler Finished");
    return 0;
  }

  static void
  get_stats()
  {
    char buffer[2024];
    int bsize = 2024;
    char *str;

    //add interface name
    //add inf speed
    
    str = getFile("/proc/net/dev", buffer, bsize);
    TSDebug(DEBUG_TAG, "dev: %s", str);

    str = getFile("/proc/loadavg", buffer, bsize);


    return;
  }

  void
  TSPluginInit(int argc, const char *argv[])
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
  
    config                      = TSmalloc(sizeof(config_t));
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
      get_stats();
        TSDebug(DEBUG_TAG, "Init complete");
  }
  