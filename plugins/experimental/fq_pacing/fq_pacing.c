/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ts/ts.h>
#include <ts/remap.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <limits.h>
#include <time.h>

static const char *PLUGIN_NAME = "fq_pacing";

// Sanity check max rate at 100Gbps
#define MAX_PACING_RATE 100000000000
#define STR_BUFFER_SIZE 65536
// How often to update the current bandwidth (1s)
#define BANDWIDTH_TIMEOUT 1000
typedef struct fq_pacing_config {
  unsigned long pacing_rate;
  unsigned long max_rate;
  unsigned long current_rate;
  unsigned long prev_bytes;
  time_t prev_timestamp_sec;
  char sys_fs[PATH_MAX];
} fq_pacing_cfg_t;

typedef struct fq_pacing_cont {
  int client_fd;
} fq_pacing_cont_t;

// Copied from ts/ink_sock.cc since that function is not exposed to plugins
int
safe_setsockopt(int s, int level, int optname, char *optval, int optlevel)
{
  int r;
  do {
    r = setsockopt(s, level, optname, optval, optlevel);
  } while (r < 0 && (errno == EAGAIN || errno == EINTR));
  return r;
}

static int
fq_is_default_qdisc()
{
  TSFile f       = 0;
  ssize_t s      = 0;
  char buffer[5] = {};
  int rc         = 0;

  f = TSfopen("/proc/sys/net/core/default_qdisc", "r");
  if (!f) {
    return 0;
  }

  s = TSfread(f, buffer, sizeof(buffer));
  if (s > 0) {
    buffer[s] = 0;
  } else {
    TSfclose(f);
    return 0;
  }

  if (buffer[2] == '\n') {
    buffer[2] = 0;
  }

  rc = (strncmp(buffer, "fq", sizeof(buffer)) == 0);
  TSfclose(f);
  return (rc);
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"fq_pacing";
  info.vendor_name   = (char *)"Cisco Systems";
  info.support_email = (char *)"omdbuild@cisco.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[fq_pacing] plugin registration failed");
  }
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[fq_pacing] - Invalid TSRemapInterface argument", (size_t)(errbuf_size - 1));
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[TSRemapInit] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  if (!fq_is_default_qdisc()) {
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - fq qdisc is not active");
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "plugin is successfully initialized");
  return TS_SUCCESS;
}

static int
bandwidth_tracking_cont(TSCont contp, TSEvent event, void *edata)
{
  fq_pacing_cfg_t *txn_data = TSContDataGet(contp);
  unsigned long val         = 0;
  time_t cur_ts             = time(NULL);
  TSDebug(PLUGIN_NAME, "bw track for %s cur_rate: %ld, prev_bytes: %ld, curts: %ld, prevts: %ld", txn_data->sys_fs,
          txn_data->current_rate, txn_data->prev_bytes, cur_ts, txn_data->prev_timestamp_sec);

  TSFile f = 0;
  size_t s = 0;
  f        = TSfopen(txn_data->sys_fs, "r");
  if (f) {
    char buffer[STR_BUFFER_SIZE];
    memset(&buffer[0], 0, STR_BUFFER_SIZE);
    s = TSfread(f, buffer, STR_BUFFER_SIZE);
    if (s > 0) {
      buffer[s] = 0;
    } else {
      buffer[0] = 0;
    }
    TSfclose(f);
    val = strtoul(&buffer[0], NULL, 0);

    TSDebug(PLUGIN_NAME, "stat bytes: %ld", val);
    if (txn_data->prev_bytes == 0) {
      txn_data->prev_bytes         = val;
      txn_data->current_rate       = val;
      txn_data->prev_timestamp_sec = cur_ts;
    } else {
      /* Only update the rate and bytes if we have past 1 second, otherwise wait for the next cont */
      if ((cur_ts - txn_data->prev_timestamp_sec) > 0) {
        txn_data->current_rate       = (val - txn_data->prev_bytes) / (cur_ts - txn_data->prev_timestamp_sec);
        txn_data->prev_bytes         = val;
        txn_data->prev_timestamp_sec = cur_ts;
        TSDebug(PLUGIN_NAME, "current_rate: %ld, prev_bytes: %ld", txn_data->current_rate, txn_data->prev_bytes);
      }
    }
  } else {
    TSDebug(PLUGIN_NAME, "unable to find %s\n", txn_data->sys_fs);
  }

  TSContScheduleOnPool(contp, BANDWIDTH_TIMEOUT, TS_THREAD_POOL_TASK);
  return 0;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  fq_pacing_cfg_t *cfg = NULL;
  bool seen_inf        = false;
  TSDebug(PLUGIN_NAME, "Instantiating a new remap.config plugin rule");

  cfg = TSmalloc(sizeof(fq_pacing_cfg_t));
  memset(cfg, 0, sizeof(*cfg));

  if (argc > 1) {
    int c;
    static const struct option longopts[] = {
      {"rate",      required_argument, NULL, 'r'},
      {"max-rate",  required_argument, NULL, 'm'},
      {"interface", required_argument, NULL, 'i'},
      {NULL,        0,                 NULL, 0  }
    };

    cfg->pacing_rate = 0;
    cfg->max_rate    = 0;

    while ((c = getopt_long(argc, (char *const *)argv, "-r:-m:-i:", longopts, NULL)) != -1) {
      switch (c) {
      case 'r':
        errno            = 0;
        cfg->pacing_rate = strtoul(optarg, NULL, 0);
        if (errno != 0) {
          snprintf(errbuf, errbuf_size - 1, "[TsRemapNewInstance] input pacing value is not a valid positive integer");
          return TS_ERROR;
        }

        break;
      case 'm':
        errno         = 0;
        cfg->max_rate = strtoul(optarg, NULL, 0);
        if (errno != 0) {
          snprintf(errbuf, errbuf_size - 1, "[TsRemapNewInstance] input maximum rate value is not a valid positive integer");
          return TS_ERROR;
        }
        break;
      case 'i':
        memset(cfg->sys_fs, 0, PATH_MAX);
        snprintf(cfg->sys_fs, PATH_MAX, "/sys/class/net/%s/statistics/rx_bytes", optarg);
        seen_inf = true;
        break;
      }
    }
  }

  if (cfg->pacing_rate > MAX_PACING_RATE) {
    snprintf(errbuf, errbuf_size - 1, "[TsRemapNewInstance] input pacing value is too large (%lu), max(%lu)", cfg->pacing_rate,
             MAX_PACING_RATE);
    return TS_ERROR;
  }

  if ((cfg->max_rate != 0 && seen_inf == false) || (seen_inf == true && cfg->max_rate == 0)) {
    snprintf(errbuf, errbuf_size - 1,
             "[RsRemapNewInstance] If using maximum rate limiting both maximum and interface params are required");
    return TS_ERROR;
  }

  cfg->current_rate       = 0;
  cfg->prev_bytes         = 0;
  cfg->prev_timestamp_sec = 0;
  *ih                     = (void *)cfg;
  TSDebug(PLUGIN_NAME, "Setting pacing rate to %lu, max rate to %lu", cfg->pacing_rate, cfg->max_rate);

  if (cfg->max_rate > 0) {
    // Create the bandwidth tracker cont, updates once every BW_TIMEOUT
    TSCont bw_cont = TSContCreate(bandwidth_tracking_cont, TSMutexCreate());
    TSContDataSet(bw_cont, (void *)cfg);
    TSContScheduleOnPool(bw_cont, 0, TS_THREAD_POOL_TASK);
  }

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *instance)
{
  TSError("[fq_pacing] Cleaning up...");

  if (instance != NULL) {
    TSfree((fq_pacing_cfg_t *)instance);
  }
}

static int
reset_pacing_cont(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp             = (TSHttpTxn)edata;
  fq_pacing_cont_t *txn_data = TSContDataGet(contp);

#ifdef SO_MAX_PACING_RATE
  unsigned int pacing_off = ~0U;
  if (txn_data->client_fd > 0) {
    int res = 0;
    res     = safe_setsockopt(txn_data->client_fd, SOL_SOCKET, SO_MAX_PACING_RATE, (char *)&pacing_off, sizeof(pacing_off));
    // EBADF indicates possible client abort
    if ((res < 0) && (errno != EBADF)) {
      TSError("[fq_pacing] Error disabling SO_MAX_PACING_RATE, errno=%d", errno);
    }
  }
#endif

  TSfree(txn_data);
  TSContDestroy(contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  if (TSHttpTxnClientProtocolStackContains(txnp, TS_PROTO_TAG_HTTP_2_0) != NULL) {
    TSDebug(PLUGIN_NAME, "Skipping plugin execution for HTTP/2 requests");
    return TSREMAP_NO_REMAP;
  }

  int client_fd = 0;
  if (TSHttpTxnClientFdGet(txnp, &client_fd) != TS_SUCCESS) {
    TSError("[fq_pacing] Error getting client fd");
  }

#ifdef SO_MAX_PACING_RATE
  fq_pacing_cfg_t *cfg = (fq_pacing_cfg_t *)instance;
  int res              = 0;

  // If the max_rate has not been set, we are not using bandwidth tracking, so set pacing, otherwise check our current rate vs max
  if ((cfg->max_rate == 0) || (cfg->current_rate > cfg->max_rate)) {
    res = safe_setsockopt(client_fd, SOL_SOCKET, SO_MAX_PACING_RATE, (char *)&cfg->pacing_rate, sizeof(cfg->pacing_rate));
    if ((res < 0)) {
      TSError("[fq_pacing] Error setting SO_MAX_PACING_RATE, errno=%d", errno);
    }
  }
#endif

  // Reset pacing at end of transaction in case session is
  // reused for another delivery service w/o pacing
  TSCont cont = TSContCreate(reset_pacing_cont, NULL);

  fq_pacing_cont_t *txn_data = TSmalloc(sizeof(fq_pacing_cont_t));
  txn_data->client_fd        = client_fd;
  TSContDataSet(cont, txn_data);

  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, cont);

  return TSREMAP_NO_REMAP;
}
