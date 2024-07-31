/** @file

  TLSBasicSupport implements common methods and members to
  support basic features on TLS connections

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

#pragma once

#include <openssl/ssl.h>

#include "tscore/ink_hrtime.h"
#include "iocore/net/SSLTypes.h"

using TLSHandle = SSL *;

class TLSBasicSupport
{
public:
  virtual ~TLSBasicSupport() = default;

  static void             initialize();
  static TLSBasicSupport *getInstance(SSL *ssl);
  static void             bind(SSL *ssl, TLSBasicSupport *srs);
  static void             unbind(SSL *ssl);

  TLSHandle   get_tls_handle() const;
  const char *get_tls_protocol_name() const;
  const char *get_tls_cipher_suite() const;
  const char *get_tls_curve() const;
  ink_hrtime  get_tls_handshake_begin_time() const;
  ink_hrtime  get_tls_handshake_end_time() const;

  void set_valid_tls_version_min(int min);
  void set_valid_tls_version_max(int max);
  void set_valid_tls_protocols(unsigned long proto_mask, unsigned long max_mask);

protected:
  void clear();

  virtual SSL         *_get_ssl_object() const = 0;
  virtual ssl_curve_id _get_tls_curve() const  = 0;

  void _record_tls_handshake_begin_time();
  void _record_tls_handshake_end_time();

private:
  static int _ex_data_index;

  ink_hrtime _tls_handshake_begin_time = 0;
  ink_hrtime _tls_handshake_end_time   = 0;
};
