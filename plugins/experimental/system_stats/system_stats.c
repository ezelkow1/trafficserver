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


  typedef struct 
  {
    unsigned int recordTypes;
    int txn_slot;
    TSStatPersistence persist_type;
    TSMutex stat_creation_mutex;
  } config_t;

  typedef struct 
  {
    TSVConn net_vc;
    TSVIO read_vio;
    TSVIO write_vio;
  
    TSIOBuffer req_buffer;
    TSIOBuffer resp_buffer;
    TSIOBufferReader resp_reader;
    TSHttpTxn txn;

    int output_bytes;
    int body_written;
    
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

  static int stats_add_data_to_resp_buffer(const char *s, stats_state *my_state)
  {
    int s_len = strlen(s);
  
    TSIOBufferWrite(my_state->resp_buffer, s, s_len);
  
    return s_len;
  }

  static void stats_cleanup(TSCont contp, stats_state *my_state)
  {
    if (my_state->req_buffer) {
      TSIOBufferDestroy(my_state->req_buffer);
      my_state->req_buffer = NULL;
    }

    if (my_state->resp_buffer) {
      TSIOBufferDestroy(my_state->resp_buffer);
      my_state->resp_buffer = NULL;
    }
    TSVConnClose(my_state->net_vc);
    TSfree(my_state);
    TSContDestroy(contp);
  }

  static void stats_process_accept(TSCont contp, stats_state *my_state)
  {
    my_state->req_buffer  = TSIOBufferCreate();
    my_state->resp_buffer = TSIOBufferCreate();
    my_state->resp_reader = TSIOBufferReaderAlloc(my_state->resp_buffer);
    my_state->read_vio    = TSVConnRead(my_state->net_vc, contp, my_state->req_buffer, INT64_MAX);
  }

  static const char RESP_HEADER[] = "HTTP/1.0 200 Ok\r\nContent-Type: text/javascript\r\nCache-Control: no-cache\r\n\r\n";
  
  static int stats_add_resp_header(stats_state *my_state)
  {
    return stats_add_data_to_resp_buffer(RESP_HEADER, my_state);
  }
  
  #define APPEND(a) my_state->output_bytes += stats_add_data_to_resp_buffer(a, my_state)
  #define APPEND_STAT(a, fmt, v)                                                   \
    do {                                                                           \
      char b[256];                                                                 \
      if (snprintf(b, sizeof(b), "\"%s\": \"" fmt "\",\n", a, v) < (int)sizeof(b)) \
        APPEND(b);                                                                 \
    } while (0)

    #define APPEND_STAT_NUMERIC(a, fmt, v)                                               \
    do {                                                                               \
      char b[256];                                                                     \
        if (snprintf(b, sizeof(b), "\"%s\": \"" fmt "\",\n", a, v) < (int)sizeof(b)) { \
          APPEND(b);                                                                   \
      }                                                                                \
    } while (0)

  static void json_out_stat(TSRecordType rec_type, void *edata, int registered, const char *name, TSRecordDataType data_type, TSRecordData *datum) 
  {
    stats_state *my_state = edata;
    int found = 0;
    int i;
  
    if (my_state->globals_cnt) {
      for (i = 0; i < my_state->globals_cnt; i++) {
        if (strstr(name, my_state->globals[i])) {
          found = 1;
          break;
        }
      }
  
      if (!found)
        return; // skip
    }
  
    switch(data_type) {
    case TS_RECORDDATATYPE_COUNTER:
      APPEND_STAT_NUMERIC(name, "%" PRIu64, datum->rec_counter); break;
    case TS_RECORDDATATYPE_INT:
      APPEND_STAT_NUMERIC(name, "%" PRIi64, datum->rec_int); break;
    case TS_RECORDDATATYPE_FLOAT:
      APPEND_STAT_NUMERIC(name, "%f", datum->rec_float); break;
    case TS_RECORDDATATYPE_STRING:
      APPEND_STAT(name, "\"%s\"", datum->rec_string); break;
    default:
      TSDebug(PLUGIN_NAME, "unkown type for %s: %d", name, data_type);
      break;
    }
  }

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

  #if 0
  static char * get_effective_host(TSHttpTxn txn)
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

  #if 0  
  static void json_out_stats(stats_state *my_state) 
  {
    const char *version;
    TSDebug(PLUGIN_NAME, "recordTypes: '0x%x'", my_state->recordTypes);
    APPEND("{ \"ats\": {\n");
          TSRecordDump(my_state->recordTypes, json_out_stat, my_state);
    version = TSTrafficServerVersionGet();
    APPEND("   \"server\": \"");
    APPEND(version);
    APPEND("\"\n");
    APPEND("  }");
  
    if (my_state->recordTypes & SYSTEM_RECORD_TYPE) {
      APPEND(",\n \"system\": {\n");
      get_stats(my_state);

      APPEND_STAT_NUMERIC("configReloadRequests", "%d", configReloadRequests);
      APPEND_STAT("lastReloadRequest", "%" PRIi64, (long long)lastReloadRequest);
      APPEND_STAT_NUMERIC("configReloads", "%d", configReloads);
      APPEND_STAT_NUMERIC("lastReload", "%" PRIi64, (long long)lastReload);
      APPEND_STAT_NUMERIC("astatsLoad", "%" PRIi64, (long long)astatsLoad);
      APPEND("\"something\": \"here\"");
      APPEND("\n  }");
    }
  
    APPEND("\n}\n");
  }
  #endif

  static void stats_process_write(TSCont contp, TSEvent event, stats_state *my_state) 
  {
    if (event == TS_EVENT_VCONN_WRITE_READY) {
      if (my_state->body_written == 0) {
        TSDebug(PLUGIN_NAME, "plugin adding response body");
        my_state->body_written = 1;
        //json_out_stats(my_state);
        TSVIONBytesSet(my_state->write_vio, my_state->output_bytes);
      }
      TSVIOReenable(my_state->write_vio);
      TSfree(my_state->globals);
      my_state->globals = NULL;
      TSfree(my_state->query);
      my_state->query = NULL;
    } else if (TS_EVENT_VCONN_WRITE_COMPLETE)
      stats_cleanup(contp, my_state);
    else if (event == TS_EVENT_ERROR)
      TSError("stats_process_write: Received TS_EVENT_ERROR\n");
    else
      TSReleaseAssert(!"Unexpected Event");
  }
  
  static void stats_process_read(TSCont contp, TSEvent event, stats_state *my_state)
  {
    TSDebug(PLUGIN_NAME, "stats_process_read(%d)", event);
    if (event == TS_EVENT_VCONN_READ_READY) {
      my_state->output_bytes = stats_add_resp_header(my_state);
      TSVConnShutdown(my_state->net_vc, 1, 0);
      my_state->write_vio = TSVConnWrite(my_state->net_vc, contp, my_state->resp_reader, INT64_MAX);
    } else if (event == TS_EVENT_ERROR) {
      TSError("[%s] stats_process_read: Received TS_EVENT_ERROR", PLUGIN_NAME);
    } else if (event == TS_EVENT_VCONN_EOS) {
      /* client may end the connection, simply return */
      return;
    } else if (event == TS_EVENT_NET_ACCEPT_FAILED) {
      TSError("[%s] stats_process_read: Received TS_EVENT_NET_ACCEPT_FAILED", PLUGIN_NAME);
    } else {
      printf("Unexpected Event %d\n", event);
      TSReleaseAssert(!"Unexpected Event");
    }
  }
  
  static int stats_dostuff(TSCont contp, TSEvent event, void *edata) 
  {
    stats_state *my_state = TSContDataGet(contp);
    if (event == TS_EVENT_NET_ACCEPT) {
      my_state->net_vc = (TSVConn) edata;
      stats_process_accept(contp, my_state);
    } else if (edata == my_state->read_vio)
      stats_process_read(contp, event, my_state);
    else if (edata == my_state->write_vio)
      stats_process_write(contp, event, my_state);
    else
      TSReleaseAssert(!"Unexpected Event");
  
    return 0;
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
  