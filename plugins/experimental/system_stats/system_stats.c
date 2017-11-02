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
    unsigned int recordTypes;
    int txn_slot;
    TSStatPersistence persist_type;
    TSMutex stat_creation_mutex;
  } config_t;

  typedef struct {
    TSVConn net_vc;
    TSVIO read_vio;
    TSVIO write_vio;
  
    TSIOBuffer req_buffer;
    TSIOBuffer resp_buffer;
    TSIOBufferReader resp_reader;
  
    int output_bytes;
    int body_written;
    
    int globals_cnt;
    char **globals;
    char *interfaceName;
    char *query;
    int speed;
    unsigned int recordTypes;
  } stats_state;

  #define APPEND(a) my_state->output_bytes += stats_add_data_to_resp_buffer(a, my_state)
  #define APPEND_STAT(a, fmt, v) do { \
      char b[3048]; \
      if (snprintf(b, sizeof(b), "   \"%s\": " fmt ",\n", a, v) < sizeof(b)) \
      APPEND(b); \
  } while(0)

#if 0
  static char * nstr(const char *s) {
    char *mys = (char *)TSmalloc(strlen(s)+1);
    strcpy(mys, s);
    return mys;
  }
#endif
  static char * nstrl(const char *s, int len) {
    char *mys = (char *)TSmalloc(len + 1);
    memcpy(mys, s, len);
    mys[len] = 0;
    return mys;
  }

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
  getSpeed(char *inf) 
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
#if 0
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
#endif
  static char *
  get_query(TSHttpTxn txn)
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

  static void
  get_stats(stats_state *my_state, TSHttpTxn txn)
  {
    char buffer[2024];
    int bsize = 2024;
    char *str;
    char *query;

    //add interface name
    //add inf speed
    query = get_query(txn);
    stats_fillState(my_state, query, strlen(query));

    str = getFile("/proc/net/dev", buffer, bsize);
    TSDebug(DEBUG_TAG, "dev: %s", str);

    str = getFile("/proc/loadavg", buffer, bsize);

    my_state->speed = getSpeed(my_state->interfaceName);


    return;
  }

  static int
  handle_read_req_hdr(TSCont cont, TSEvent event ATS_UNUSED, void *edata)
  {
    TSHttpTxn txn = (TSHttpTxn)edata;
    config_t *config;
    TSEvent reenable = TS_EVENT_HTTP_CONTINUE;
    stats_state *my_state;

    config = (config_t *)TSContDataGet(cont);

    my_state = (stats_state *) TSmalloc(sizeof(*my_state));
    memset(my_state, 0, sizeof(*my_state));
  
    my_state->recordTypes = config->recordTypes;
    get_stats(my_state, txn);
    TSDebug(DEBUG_TAG, "%s(): inf %s", __FUNCTION__, my_state->interfaceName );
    TSDebug(DEBUG_TAG, "%s(): speed: %d", __FUNCTION__, my_state->speed);
    
    TSDebug(DEBUG_TAG, "Read Req Handler Finished");
    TSHttpTxnReenable(txn, reenable);
    return 0;
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
      //get_stats();
        TSDebug(DEBUG_TAG, "Init complete");
  }
  