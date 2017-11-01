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

#define PLUGIN_NAME "system_stats"
#define DEBUG_TAG PLUGIN_NAME

typedef struct {
    int txn_slot;
    TSStatPersistence persist_type;
    TSMutex stat_creation_mutex;
  } config_t;

  void
  TSPluginInit(int argc, const char *argv[])
  {
    TSPluginRegistrationInfo info;
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
/****
 * Do an initial poll here of all stats to save
 * an initial state
 * ****/
    TSDebug(DEBUG_TAG, "Init complete");
  }
  