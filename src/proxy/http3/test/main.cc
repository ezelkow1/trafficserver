/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

// To make compile faster
// https://github.com/philsquared/Catch/blob/master/docs/slow-compiles.md
#include "iocore/eventsystem/EventSystem.h"
#include "iocore/eventsystem/RecProcess.h"
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "tscore/Layout.h"
#include "tscore/Diags.h"

#include "records/RecordsConfig.h"
#include "proxy/http3/Http3Config.h"

#define TEST_THREADS 1

struct EventProcessorListener : Catch::TestEventListenerBase {
  using TestEventListenerBase::TestEventListenerBase; // inherit constructor

  void
  testRunStarting(Catch::TestRunInfo const &testRunInfo) override
  {
    BaseLogFile *base_log_file = new BaseLogFile("stderr");
    DiagsPtr::set(new Diags(testRunInfo.name, "" /* tags */, "" /* actions */, base_log_file));
    diags()->activate_taglist("vv_quic|quic", DiagsTagType_Debug);
    diags()->config.enabled(DiagsTagType_Debug, 1);
    diags()->show_location = SHOW_LOCATION_DEBUG;

    Layout::create();
    RecProcessInit();
    LibRecordsConfigInit();

    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    eventProcessor.start(TEST_THREADS);

    Thread *main_thread = new EThread;
    main_thread->set_specific();

    ts::Http3Config::startup();
  }
};
CATCH_REGISTER_LISTENER(EventProcessorListener);
