/** @file

  A brief file description

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

#include "tscore/ink_defs.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_inet.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string_view>
#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/HdrToken.h"
#include "tscore/Diags.h"

using namespace std::literals;

/***********************************************************************
 *                                                                     *
 *                    C O M P I L E    O P T I O N S                   *
 *                                                                     *
 ***********************************************************************/

#define ENABLE_PARSER_FAST_PATHS 1

/***********************************************************************
 *                                                                     *
 *                          C O N S T A N T S                          *
 *                                                                     *
 ***********************************************************************/

c_str_view HTTP_METHOD_CONNECT;
c_str_view HTTP_METHOD_DELETE;
c_str_view HTTP_METHOD_GET;
c_str_view HTTP_METHOD_HEAD;
c_str_view HTTP_METHOD_OPTIONS;
c_str_view HTTP_METHOD_POST;
c_str_view HTTP_METHOD_PURGE;
c_str_view HTTP_METHOD_PUT;
c_str_view HTTP_METHOD_TRACE;
c_str_view HTTP_METHOD_PUSH;

int HTTP_WKSIDX_CONNECT;
int HTTP_WKSIDX_DELETE;
int HTTP_WKSIDX_GET;
int HTTP_WKSIDX_HEAD;
int HTTP_WKSIDX_OPTIONS;
int HTTP_WKSIDX_POST;
int HTTP_WKSIDX_PURGE;
int HTTP_WKSIDX_PUT;
int HTTP_WKSIDX_TRACE;
int HTTP_WKSIDX_PUSH;
int HTTP_WKSIDX_METHODS_CNT = 0;

c_str_view HTTP_VALUE_BYTES;
c_str_view HTTP_VALUE_CHUNKED;
c_str_view HTTP_VALUE_CLOSE;
c_str_view HTTP_VALUE_COMPRESS;
c_str_view HTTP_VALUE_DEFLATE;
c_str_view HTTP_VALUE_GZIP;
c_str_view HTTP_VALUE_BROTLI;
c_str_view HTTP_VALUE_IDENTITY;
c_str_view HTTP_VALUE_KEEP_ALIVE;
c_str_view HTTP_VALUE_MAX_AGE;
c_str_view HTTP_VALUE_MAX_STALE;
c_str_view HTTP_VALUE_MIN_FRESH;
c_str_view HTTP_VALUE_MUST_REVALIDATE;
c_str_view HTTP_VALUE_NONE;
c_str_view HTTP_VALUE_NO_CACHE;
c_str_view HTTP_VALUE_NO_STORE;
c_str_view HTTP_VALUE_NO_TRANSFORM;
c_str_view HTTP_VALUE_ONLY_IF_CACHED;
c_str_view HTTP_VALUE_PRIVATE;
c_str_view HTTP_VALUE_PROXY_REVALIDATE;
c_str_view HTTP_VALUE_PUBLIC;
c_str_view HTTP_VALUE_S_MAXAGE;
c_str_view HTTP_VALUE_NEED_REVALIDATE_ONCE;
c_str_view HTTP_VALUE_100_CONTINUE;
// Cache-control: extension "need-revalidate-once" is used internally by T.S.
// to invalidate a document, and it is not returned/forwarded.
// If a cached document has this extension set (ie, is invalidated),
// then the T.S. needs to revalidate the document once before returning it.
// After a successful revalidation, the extension will be removed by T.S.
// To set or unset this directive should be done via the following two
// function:
//      set_cooked_cc_need_revalidate_once()
//      unset_cooked_cc_need_revalidate_once()
// To test, use regular Cache-control testing functions, eg,
//      is_cache_control_set(HTTP_VALUE_NEED_REVALIDATE_ONCE)

Arena *const HTTPHdr::USE_HDR_HEAP_MAGIC = reinterpret_cast<Arena *>(1);

namespace
{
DbgCtl dbg_ctl_http{"http"};

} // end anonymous namespace

/***********************************************************************
 *                                                                     *
 *                         M A I N    C O D E                          *
 *                                                                     *
 ***********************************************************************/

void
http_hdr_adjust(HTTPHdrImpl * /* hdrp ATS_UNUSED */, int32_t /* offset ATS_UNUSED */, int32_t /* length ATS_UNUSED */,
                int32_t /* delta ATS_UNUSED */)
{
  ink_release_assert(!"http_hdr_adjust not implemented");
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_init()
{
  static int init = 1;

  if (init) {
    init = 0;

    mime_init();
    url_init();

    HTTP_METHOD_CONNECT = hdrtoken_string_to_wks_sv("CONNECT");
    HTTP_METHOD_DELETE  = hdrtoken_string_to_wks_sv("DELETE");
    HTTP_METHOD_GET     = hdrtoken_string_to_wks_sv("GET");
    HTTP_METHOD_HEAD    = hdrtoken_string_to_wks_sv("HEAD");
    HTTP_METHOD_OPTIONS = hdrtoken_string_to_wks_sv("OPTIONS");
    HTTP_METHOD_POST    = hdrtoken_string_to_wks_sv("POST");
    HTTP_METHOD_PURGE   = hdrtoken_string_to_wks_sv("PURGE");
    HTTP_METHOD_PUT     = hdrtoken_string_to_wks_sv("PUT");
    HTTP_METHOD_TRACE   = hdrtoken_string_to_wks_sv("TRACE");
    HTTP_METHOD_PUSH    = hdrtoken_string_to_wks_sv("PUSH");

    // HTTP methods index calculation. Don't forget to count them!
    // Don't change the order of calculation! Each index has related bitmask (see http quick filter)
    HTTP_WKSIDX_CONNECT = hdrtoken_wks_to_index(HTTP_METHOD_CONNECT.c_str());
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_DELETE = hdrtoken_wks_to_index(HTTP_METHOD_DELETE.c_str());
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_GET = hdrtoken_wks_to_index(HTTP_METHOD_GET.c_str());
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_HEAD = hdrtoken_wks_to_index(HTTP_METHOD_HEAD.c_str());
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_OPTIONS = hdrtoken_wks_to_index(HTTP_METHOD_OPTIONS.c_str());
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_POST = hdrtoken_wks_to_index(HTTP_METHOD_POST.c_str());
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_PURGE = hdrtoken_wks_to_index(HTTP_METHOD_PURGE.c_str());
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_PUT = hdrtoken_wks_to_index(HTTP_METHOD_PUT.c_str());
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_TRACE = hdrtoken_wks_to_index(HTTP_METHOD_TRACE.c_str());
    HTTP_WKSIDX_METHODS_CNT++;
    HTTP_WKSIDX_PUSH = hdrtoken_wks_to_index(HTTP_METHOD_PUSH.c_str());
    HTTP_WKSIDX_METHODS_CNT++;

    HTTP_VALUE_BYTES                = hdrtoken_string_to_wks_sv("bytes");
    HTTP_VALUE_CHUNKED              = hdrtoken_string_to_wks_sv("chunked");
    HTTP_VALUE_CLOSE                = hdrtoken_string_to_wks_sv("close");
    HTTP_VALUE_COMPRESS             = hdrtoken_string_to_wks_sv("compress");
    HTTP_VALUE_DEFLATE              = hdrtoken_string_to_wks_sv("deflate");
    HTTP_VALUE_GZIP                 = hdrtoken_string_to_wks_sv("gzip");
    HTTP_VALUE_BROTLI               = hdrtoken_string_to_wks_sv("br");
    HTTP_VALUE_IDENTITY             = hdrtoken_string_to_wks_sv("identity");
    HTTP_VALUE_KEEP_ALIVE           = hdrtoken_string_to_wks_sv("keep-alive");
    HTTP_VALUE_MAX_AGE              = hdrtoken_string_to_wks_sv("max-age");
    HTTP_VALUE_MAX_STALE            = hdrtoken_string_to_wks_sv("max-stale");
    HTTP_VALUE_MIN_FRESH            = hdrtoken_string_to_wks_sv("min-fresh");
    HTTP_VALUE_MUST_REVALIDATE      = hdrtoken_string_to_wks_sv("must-revalidate");
    HTTP_VALUE_NONE                 = hdrtoken_string_to_wks_sv("none");
    HTTP_VALUE_NO_CACHE             = hdrtoken_string_to_wks_sv("no-cache");
    HTTP_VALUE_NO_STORE             = hdrtoken_string_to_wks_sv("no-store");
    HTTP_VALUE_NO_TRANSFORM         = hdrtoken_string_to_wks_sv("no-transform");
    HTTP_VALUE_ONLY_IF_CACHED       = hdrtoken_string_to_wks_sv("only-if-cached");
    HTTP_VALUE_PRIVATE              = hdrtoken_string_to_wks_sv("private");
    HTTP_VALUE_PROXY_REVALIDATE     = hdrtoken_string_to_wks_sv("proxy-revalidate");
    HTTP_VALUE_PUBLIC               = hdrtoken_string_to_wks_sv("public");
    HTTP_VALUE_S_MAXAGE             = hdrtoken_string_to_wks_sv("s-maxage");
    HTTP_VALUE_NEED_REVALIDATE_ONCE = hdrtoken_string_to_wks_sv("need-revalidate-once");
    HTTP_VALUE_100_CONTINUE         = hdrtoken_string_to_wks_sv("100-continue");
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

HTTPHdrImpl *
http_hdr_create(HdrHeap *heap, HTTPType polarity, HTTPVersion version)
{
  HTTPHdrImpl *hh;

  hh = (HTTPHdrImpl *)heap->allocate_obj(sizeof(HTTPHdrImpl), HdrHeapObjType::HTTP_HEADER);
  http_hdr_init(heap, hh, polarity, version);
  return (hh);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_init(HdrHeap *heap, HTTPHdrImpl *hh, HTTPType polarity, HTTPVersion version)
{
  memset(&(hh->u), 0, sizeof(hh->u));
  hh->m_polarity    = polarity;
  hh->m_version     = HTTP_1_0;
  hh->m_fields_impl = mime_hdr_create(heap);
  if (polarity == HTTPType::REQUEST) {
    hh->u.req.m_url_impl       = url_create(heap);
    hh->u.req.m_method_wks_idx = -1;
  }

  if (version == HTTP_2_0 || version == HTTP_3_0) {
    MIMEField *field;
    switch (polarity) {
    case HTTPType::REQUEST:
      field = mime_field_create_named(heap, hh->m_fields_impl, PSEUDO_HEADER_METHOD);
      mime_hdr_field_attach(hh->m_fields_impl, field, false, nullptr);

      field = mime_field_create_named(heap, hh->m_fields_impl, PSEUDO_HEADER_SCHEME);
      mime_hdr_field_attach(hh->m_fields_impl, field, false, nullptr);

      field = mime_field_create_named(heap, hh->m_fields_impl, PSEUDO_HEADER_AUTHORITY);
      mime_hdr_field_attach(hh->m_fields_impl, field, false, nullptr);

      field = mime_field_create_named(heap, hh->m_fields_impl, PSEUDO_HEADER_PATH);
      mime_hdr_field_attach(hh->m_fields_impl, field, false, nullptr);
      break;
    case HTTPType::RESPONSE:
      field = mime_field_create_named(heap, hh->m_fields_impl, PSEUDO_HEADER_STATUS);
      mime_hdr_field_attach(hh->m_fields_impl, field, false, nullptr);
      break;
    default:
      ink_abort("HTTPType::UNKNOWN");
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_copy_onto(HTTPHdrImpl *s_hh, HdrHeap *s_heap, HTTPHdrImpl *d_hh, HdrHeap *d_heap, bool inherit_strs)
{
  MIMEHdrImpl *s_mh, *d_mh;
  URLImpl     *s_url, *d_url;
  HTTPType     d_polarity;

  s_mh       = s_hh->m_fields_impl;
  s_url      = s_hh->u.req.m_url_impl;
  d_mh       = d_hh->m_fields_impl;
  d_url      = d_hh->u.req.m_url_impl;
  d_polarity = d_hh->m_polarity;

  ink_assert(s_hh->m_polarity != HTTPType::UNKNOWN);
  ink_assert(s_mh != nullptr);
  ink_assert(d_mh != nullptr);

  memcpy(d_hh, s_hh, sizeof(HTTPHdrImpl));
  d_hh->m_fields_impl = d_mh; // restore pre-memcpy mime impl

  if (s_hh->m_polarity == HTTPType::REQUEST) {
    if (d_polarity == HTTPType::REQUEST) {
      d_hh->u.req.m_url_impl = d_url; // restore pre-memcpy url impl
    } else {
      d_url = d_hh->u.req.m_url_impl = url_create(d_heap); // create url
    }
    url_copy_onto(s_url, s_heap, d_url, d_heap, false);
  } else if (d_polarity == HTTPType::REQUEST) {
    // gender bender.  Need to kill off old url
    url_clear(d_url);
  }

  mime_hdr_copy_onto(s_mh, s_heap, d_mh, d_heap, false);
  if (inherit_strs) {
    d_heap->inherit_string_heaps(s_heap);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

HTTPHdrImpl *
http_hdr_clone(HTTPHdrImpl *s_hh, HdrHeap *s_heap, HdrHeap *d_heap)
{
  HTTPHdrImpl *d_hh;

  // FIX: A future optimization is to copy contiguous objects with
  //      one single memcpy.  For this first optimization, we just
  //      copy each object separately.

  d_hh = http_hdr_create(d_heap, s_hh->m_polarity, s_hh->m_version);
  http_hdr_copy_onto(s_hh, s_heap, d_hh, d_heap, ((s_heap != d_heap) ? true : false));
  return (d_hh);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static inline char *
http_hdr_version_to_string(const HTTPVersion &version, char *buf9)
{
  ink_assert(version.get_major() < 10);
  ink_assert(version.get_minor() < 10);

  buf9[0] = 'H';
  buf9[1] = 'T';
  buf9[2] = 'T';
  buf9[3] = 'P';
  buf9[4] = '/';
  buf9[5] = '0' + version.get_major();
  buf9[6] = '.';
  buf9[7] = '0' + version.get_minor();
  buf9[8] = '\0';

  return (buf9);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
http_version_print(const HTTPVersion &version, char *buf, int bufsize, int *bufindex, int *dumpoffset)
{
#define TRY(x) \
  if (!x)      \
  return 0

  char tmpbuf[16];
  http_hdr_version_to_string(version, tmpbuf);
  TRY(mime_mem_print(std::string_view{tmpbuf, 8}, buf, bufsize, bufindex, dumpoffset));
  return 1;

#undef TRY
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
http_hdr_print(HTTPHdrImpl const *hdr, char *buf, int bufsize, int *bufindex, int *dumpoffset)
{
#define TRY(x) \
  if (!x)      \
  return 0

  int   tmplen;
  char  tmpbuf[32];
  char *p;

  ink_assert((hdr->m_polarity == HTTPType::REQUEST) || (hdr->m_polarity == HTTPType::RESPONSE));

  if (hdr->m_polarity == HTTPType::REQUEST) {
    if (hdr->u.req.m_ptr_method == nullptr) {
      return 1;
    }

    if ((buf != nullptr) && (*dumpoffset == 0) && (bufsize - *bufindex >= hdr->u.req.m_len_method + 1)) { // fastpath

      p = buf + *bufindex;
      memcpy(p, hdr->u.req.m_ptr_method, hdr->u.req.m_len_method);
      p         += hdr->u.req.m_len_method;
      *p++       = ' ';
      *bufindex += hdr->u.req.m_len_method + 1;

      if (hdr->u.req.m_url_impl) {
        TRY(url_print(hdr->u.req.m_url_impl, buf, bufsize, bufindex, dumpoffset));
        if (bufsize - *bufindex >= 1) {
          if (hdr->u.req.m_method_wks_idx == HTTP_WKSIDX_CONNECT) {
            *bufindex -= 1; // remove trailing slash for CONNECT request
          }
          p          = buf + *bufindex;
          *p++       = ' ';
          *bufindex += 1;
        } else {
          return 0;
        }
      }

      if (bufsize - *bufindex >= 9) {
        http_hdr_version_to_string(hdr->m_version, p);
        *bufindex += 9 - 1; // overwrite '\0';
      } else {
        TRY(http_version_print(hdr->m_version, buf, bufsize, bufindex, dumpoffset));
      }

      if (bufsize - *bufindex >= 2) {
        p          = buf + *bufindex;
        *p++       = '\r';
        *p++       = '\n';
        *bufindex += 2;
      } else {
        TRY(mime_mem_print("\r\n"sv, buf, bufsize, bufindex, dumpoffset));
      }

      TRY(mime_hdr_print(hdr->m_fields_impl, buf, bufsize, bufindex, dumpoffset));

    } else {
      TRY(
        mime_mem_print(std::string_view{hdr->u.req.m_ptr_method, static_cast<std::string_view::size_type>(hdr->u.req.m_len_method)},
                       buf, bufsize, bufindex, dumpoffset));

      TRY(mime_mem_print(" "sv, buf, bufsize, bufindex, dumpoffset));

      if (hdr->u.req.m_url_impl) {
        TRY(url_print(hdr->u.req.m_url_impl, buf, bufsize, bufindex, dumpoffset));
        TRY(mime_mem_print(" "sv, buf, bufsize, bufindex, dumpoffset));
      }

      TRY(http_version_print(hdr->m_version, buf, bufsize, bufindex, dumpoffset));

      TRY(mime_mem_print("\r\n"sv, buf, bufsize, bufindex, dumpoffset));

      TRY(mime_hdr_print(hdr->m_fields_impl, buf, bufsize, bufindex, dumpoffset));
    }

  } else { //  hdr->m_polarity == HTTPType::RESPONSE

    if ((buf != nullptr) && (*dumpoffset == 0) && (bufsize - *bufindex >= 9 + 6 + 1)) { // fastpath

      p = buf + *bufindex;
      http_hdr_version_to_string(hdr->m_version, p);
      p         += 8; // overwrite '\0' with space
      *p++       = ' ';
      *bufindex += 9;

      if (auto hdrstat{static_cast<int32_t>(http_hdr_status_get(hdr))}; hdrstat == 200) {
        *p++   = '2';
        *p++   = '0';
        *p++   = '0';
        tmplen = 3;
      } else {
        tmplen = mime_format_int(p, hdrstat, (bufsize - (p - buf)));
        ink_assert(tmplen <= 6);
        p += tmplen;
      }
      *p++       = ' ';
      *bufindex += tmplen + 1;

      if (hdr->u.resp.m_ptr_reason) {
        TRY(mime_mem_print(
          std::string_view{hdr->u.resp.m_ptr_reason, static_cast<std::string_view::size_type>(hdr->u.resp.m_len_reason)}, buf,
          bufsize, bufindex, dumpoffset));
      }

      if (bufsize - *bufindex >= 2) {
        p          = buf + *bufindex;
        *p++       = '\r';
        *p++       = '\n';
        *bufindex += 2;
      } else {
        TRY(mime_mem_print("\r\n"sv, buf, bufsize, bufindex, dumpoffset));
      }

      TRY(mime_hdr_print(hdr->m_fields_impl, buf, bufsize, bufindex, dumpoffset));

    } else {
      TRY(http_version_print(hdr->m_version, buf, bufsize, bufindex, dumpoffset));

      TRY(mime_mem_print(" "sv, buf, bufsize, bufindex, dumpoffset));

      tmplen = mime_format_int(tmpbuf, static_cast<int32_t>(http_hdr_status_get(hdr)), sizeof(tmpbuf));

      TRY(mime_mem_print(std::string_view{tmpbuf, static_cast<std::string_view::size_type>(tmplen)}, buf, bufsize, bufindex,
                         dumpoffset));

      TRY(mime_mem_print(" "sv, buf, bufsize, bufindex, dumpoffset));

      if (hdr->u.resp.m_ptr_reason) {
        TRY(mime_mem_print(
          std::string_view{hdr->u.resp.m_ptr_reason, static_cast<std::string_view::size_type>(hdr->u.resp.m_len_reason)}, buf,
          bufsize, bufindex, dumpoffset));
      }

      TRY(mime_mem_print("\r\n"sv, buf, bufsize, bufindex, dumpoffset));

      TRY(mime_hdr_print(hdr->m_fields_impl, buf, bufsize, bufindex, dumpoffset));
    }
  }

  return 1;

#undef TRY
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_describe(HdrHeapObjImpl *raw, bool recurse)
{
  HTTPHdrImpl *obj = (HTTPHdrImpl *)raw;

  if (obj->m_polarity == HTTPType::REQUEST) {
    Dbg(dbg_ctl_http, "[TYPE: REQ, V: %04X, URL: %p, METHOD: \"%.*s\", METHOD_LEN: %d, FIELDS: %p]",
        obj->m_version.get_flat_version(), obj->u.req.m_url_impl, obj->u.req.m_len_method,
        (obj->u.req.m_ptr_method ? obj->u.req.m_ptr_method : "NULL"), obj->u.req.m_len_method, obj->m_fields_impl);
    if (recurse) {
      if (obj->u.req.m_url_impl) {
        obj_describe(obj->u.req.m_url_impl, recurse);
      }
      if (obj->m_fields_impl) {
        obj_describe(obj->m_fields_impl, recurse);
      }
    }
  } else {
    Dbg(dbg_ctl_http, "[TYPE: RSP, V: %04X, STATUS: %d, REASON: \"%.*s\", REASON_LEN: %d, FIELDS: %p]",
        obj->m_version.get_flat_version(), obj->u.resp.m_status, obj->u.resp.m_len_reason,
        (obj->u.resp.m_ptr_reason ? obj->u.resp.m_ptr_reason : "NULL"), obj->u.resp.m_len_reason, obj->m_fields_impl);
    if (recurse) {
      if (obj->m_fields_impl) {
        obj_describe(obj->m_fields_impl, recurse);
      }
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HTTPHdr::length_get() const
{
  int length = 0;

  if (m_http->m_polarity == HTTPType::REQUEST) {
    if (m_http->u.req.m_ptr_method) {
      length = m_http->u.req.m_len_method;
    } else {
      length = 0;
    }

    length += 1; // " "

    if (m_http->u.req.m_url_impl) {
      length += url_length_get(m_http->u.req.m_url_impl);
    }

    length += 1; // " "

    length += 8; // HTTP/%d.%d

    length += 2; // "\r\n"
  } else if (m_http->m_polarity == HTTPType::RESPONSE) {
    if (m_http->u.resp.m_ptr_reason) {
      length = m_http->u.resp.m_len_reason;
    } else {
      length = 0;
    }

    length += 8; // HTTP/%d.%d

    length += 1; // " "

    length += 3; // status

    length += 1; // " "

    length += 2; // "\r\n"
  }

  length += mime_hdr_length_get(m_http->m_fields_impl);

  return length;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_type_set(HTTPHdrImpl *hh, HTTPType type)
{
  hh->m_polarity = type;
}

/*-------------------------------------------------------------------------
  RFC2616 specifies that HTTP version is of the format <major>.<minor>
  in the request line.  However, the features supported and in use are
  for versions 1.0, 1.1 and 2.0 (with HTTP/3.0 being developed). HTTP/2.0
  and HTTP/3.0 are both negotiated using ALPN over TLS and not via the HTTP
  request line thus leaving the versions supported on the request line to be
  HTTP/1.0 and HTTP/1.1 alone. This utility checks if the HTTP Version
  received in the request line is one of these and returns false otherwise
  -------------------------------------------------------------------------*/

bool
is_http1_version(const uint8_t major, const uint8_t minor)
{
  // Return true if 1.1 or 1.0
  return (major == 1) && (minor == 1 || minor == 0);
}

bool
is_http1_hdr_version_supported(const HTTPVersion &http_version)
{
  return is_http1_version(http_version.get_major(), http_version.get_minor());
}

bool
http_hdr_version_set(HTTPHdrImpl *hh, const HTTPVersion &ver)
{
  hh->m_version = ver;
  return is_http1_version(ver.get_major(), ver.get_minor());
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

std::string_view
http_hdr_method_get(HTTPHdrImpl *hh)
{
  const char *str;
  int         length;

  ink_assert(hh->m_polarity == HTTPType::REQUEST);

  if (hh->u.req.m_method_wks_idx >= 0) {
    str    = hdrtoken_index_to_wks(hh->u.req.m_method_wks_idx);
    length = hdrtoken_index_to_length(hh->u.req.m_method_wks_idx);
  } else {
    str    = hh->u.req.m_ptr_method;
    length = hh->u.req.m_len_method;
  }

  return std::string_view{str, static_cast<std::string_view::size_type>(length)};
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_method_set(HdrHeap *heap, HTTPHdrImpl *hh, std::string_view method, int16_t method_wks_idx, bool must_copy)
{
  ink_assert(hh->m_polarity == HTTPType::REQUEST);

  hh->u.req.m_method_wks_idx = method_wks_idx;
  mime_str_u16_set(heap, method, &(hh->u.req.m_ptr_method), &(hh->u.req.m_len_method), must_copy);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_url_set(HdrHeap *heap, HTTPHdrImpl *hh, URLImpl *url)
{
  ink_assert(hh->m_polarity == HTTPType::REQUEST);
  if (hh->u.req.m_url_impl != url) {
    if (hh->u.req.m_url_impl != nullptr) {
      heap->deallocate_obj(hh->u.req.m_url_impl);
    }
    // Clone into new heap if the URL was allocated against a different heap
    if (reinterpret_cast<char *>(url) < heap->m_data_start || reinterpret_cast<char *>(url) >= heap->m_free_start) {
      hh->u.req.m_url_impl = static_cast<URLImpl *>(heap->allocate_obj(url->m_length, static_cast<HdrHeapObjType>(url->m_type)));
      memcpy(hh->u.req.m_url_impl, url, url->m_length);
      // Make sure there is a read_write heap
      if (heap->m_read_write_heap.get() == nullptr) {
        int url_string_length   = url->strings_length();
        heap->m_read_write_heap = HdrStrHeap::alloc(url_string_length);
      }
      hh->u.req.m_url_impl->rehome_strings(heap);
    } else {
      hh->u.req.m_url_impl = url;
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_status_set(HTTPHdrImpl *hh, HTTPStatus status)
{
  ink_assert(hh->m_polarity == HTTPType::RESPONSE);
  hh->u.resp.m_status = static_cast<int16_t>(status);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

std::string_view
http_hdr_reason_get(HTTPHdrImpl *hh)
{
  ink_assert(hh->m_polarity == HTTPType::RESPONSE);
  return std::string_view{hh->u.resp.m_ptr_reason, static_cast<std::string_view::size_type>(hh->u.resp.m_len_reason)};
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
http_hdr_reason_set(HdrHeap *heap, HTTPHdrImpl *hh, std::string_view value, bool must_copy)
{
  ink_assert(hh->m_polarity == HTTPType::RESPONSE);
  mime_str_u16_set(heap, value, &(hh->u.resp.m_ptr_reason), &(hh->u.resp.m_len_reason), must_copy);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

const char *
http_hdr_reason_lookup(HTTPStatus status)
{
#define HTTP_STATUS_ENTRY(value, reason) \
  case value:                            \
    return #reason

  switch (static_cast<int>(status)) {
    HTTP_STATUS_ENTRY(0, None);                  // TS_HTTP_STATUS_NONE
    HTTP_STATUS_ENTRY(100, Continue);            // [RFC2616]
    HTTP_STATUS_ENTRY(101, Switching Protocols); // [RFC2616]
    HTTP_STATUS_ENTRY(102, Processing);          // [RFC2518]
    HTTP_STATUS_ENTRY(103, Early Hints);         // TODO: add RFC number
    // 103-199 Unassigned
    HTTP_STATUS_ENTRY(200, OK);                              // [RFC2616]
    HTTP_STATUS_ENTRY(201, Created);                         // [RFC2616]
    HTTP_STATUS_ENTRY(202, Accepted);                        // [RFC2616]
    HTTP_STATUS_ENTRY(203, Non - Authoritative Information); // [RFC2616]
    HTTP_STATUS_ENTRY(204, No Content);                      // [RFC2616]
    HTTP_STATUS_ENTRY(205, Reset Content);                   // [RFC2616]
    HTTP_STATUS_ENTRY(206, Partial Content);                 // [RFC2616]
    HTTP_STATUS_ENTRY(207, Multi - Status);                  // [RFC4918]
    HTTP_STATUS_ENTRY(208, Already Reported);                // [RFC5842]
    // 209-225 Unassigned
    HTTP_STATUS_ENTRY(226, IM Used); // [RFC3229]
    // 227-299 Unassigned
    HTTP_STATUS_ENTRY(300, Multiple Choices);  // [RFC2616]
    HTTP_STATUS_ENTRY(301, Moved Permanently); // [RFC2616]
    HTTP_STATUS_ENTRY(302, Found);             // [RFC2616]
    HTTP_STATUS_ENTRY(303, See Other);         // [RFC2616]
    HTTP_STATUS_ENTRY(304, Not Modified);      // [RFC2616]
    HTTP_STATUS_ENTRY(305, Use Proxy);         // [RFC2616]
    // 306 Reserved                             // [RFC2616]
    HTTP_STATUS_ENTRY(307, Temporary Redirect); // [RFC2616]
    HTTP_STATUS_ENTRY(308, Permanent Redirect); // [RFC-reschke-http-status-308-07]
    // 309-399 Unassigned
    HTTP_STATUS_ENTRY(400, Bad Request);                     // [RFC2616]
    HTTP_STATUS_ENTRY(401, Unauthorized);                    // [RFC2616]
    HTTP_STATUS_ENTRY(402, Payment Required);                // [RFC2616]
    HTTP_STATUS_ENTRY(403, Forbidden);                       // [RFC2616]
    HTTP_STATUS_ENTRY(404, Not Found);                       // [RFC2616]
    HTTP_STATUS_ENTRY(405, Method Not Allowed);              // [RFC2616]
    HTTP_STATUS_ENTRY(406, Not Acceptable);                  // [RFC2616]
    HTTP_STATUS_ENTRY(407, Proxy Authentication Required);   // [RFC2616]
    HTTP_STATUS_ENTRY(408, Request Timeout);                 // [RFC2616]
    HTTP_STATUS_ENTRY(409, Conflict);                        // [RFC2616]
    HTTP_STATUS_ENTRY(410, Gone);                            // [RFC2616]
    HTTP_STATUS_ENTRY(411, Length Required);                 // [RFC2616]
    HTTP_STATUS_ENTRY(412, Precondition Failed);             // [RFC2616]
    HTTP_STATUS_ENTRY(413, Request Entity Too Large);        // [RFC2616]
    HTTP_STATUS_ENTRY(414, Request - URI Too Long);          // [RFC2616]
    HTTP_STATUS_ENTRY(415, Unsupported Media Type);          // [RFC2616]
    HTTP_STATUS_ENTRY(416, Requested Range Not Satisfiable); // [RFC2616]
    HTTP_STATUS_ENTRY(417, Expectation Failed);              // [RFC2616]
    HTTP_STATUS_ENTRY(422, Unprocessable Entity);            // [RFC4918]
    HTTP_STATUS_ENTRY(423, Locked);                          // [RFC4918]
    HTTP_STATUS_ENTRY(424, Failed Dependency);               // [RFC4918]
    // 425 Reserved                           // [RFC2817]
    HTTP_STATUS_ENTRY(426, Upgrade Required); // [RFC2817]
    // 427 Unassigned
    HTTP_STATUS_ENTRY(428, Precondition Required); // [RFC6585]
    HTTP_STATUS_ENTRY(429, Too Many Requests);     // [RFC6585]
    // 430 Unassigned
    HTTP_STATUS_ENTRY(431, Request Header Fields Too Large); // [RFC6585]
    // 432-499 Unassigned
    HTTP_STATUS_ENTRY(500, Internal Server Error);      // [RFC2616]
    HTTP_STATUS_ENTRY(501, Not Implemented);            // [RFC2616]
    HTTP_STATUS_ENTRY(502, Bad Gateway);                // [RFC2616]
    HTTP_STATUS_ENTRY(503, Service Unavailable);        // [RFC2616]
    HTTP_STATUS_ENTRY(504, Gateway Timeout);            // [RFC2616]
    HTTP_STATUS_ENTRY(505, HTTP Version Not Supported); // [RFC2616]
    HTTP_STATUS_ENTRY(506, Variant Also Negotiates);    // [RFC2295]
    HTTP_STATUS_ENTRY(507, Insufficient Storage);       // [RFC4918]
    HTTP_STATUS_ENTRY(508, Loop Detected);              // [RFC5842]
    // 509 Unassigned
    HTTP_STATUS_ENTRY(510, Not Extended);                    // [RFC2774]
    HTTP_STATUS_ENTRY(511, Network Authentication Required); // [RFC6585]
    // 512-599 Unassigned
  }

#undef HTTP_STATUS_ENTRY

  return nullptr;
}

//////////////////////////////////////////////////////
// init     first time structure setup              //
// clear    resets an already-initialized structure //
//////////////////////////////////////////////////////

void
http_parser_init(HTTPParser *parser)
{
  parser->m_parsing_http = true;
  mime_parser_init(&parser->m_mime_parser);
}

void
http_parser_clear(HTTPParser *parser)
{
  parser->m_parsing_http = true;
  mime_parser_clear(&parser->m_mime_parser);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

#define GETNEXT(label) \
  {                    \
    cur += 1;          \
    if (cur >= end) {  \
      goto label;      \
    }                  \
  }

#define GETPREV(label)      \
  {                         \
    cur -= 1;               \
    if (cur < line_start) { \
      goto label;           \
    }                       \
  }

// NOTE: end is ONE CHARACTER PAST end of string!

ParseResult
http_parser_parse_req(HTTPParser *parser, HdrHeap *heap, HTTPHdrImpl *hh, const char **start, const char *end,
                      bool must_copy_strings, bool eof, int strict_uri_parsing, size_t max_request_line_size,
                      size_t max_hdr_field_size)
{
  if (parser->m_parsing_http) {
    MIMEScanner *scanner = &parser->m_mime_parser.m_scanner;
    URLImpl     *url;

    ParseResult err;
    bool        line_is_real;
    const char *cur;
    const char *line_start;
    const char *real_end;
    const char *method_start;
    const char *method_end;
    const char *url_start;
    const char *url_end;
    const char *version_start;
    const char *version_end;

    swoc::TextView text, parsed;

    real_end = end;

  start:
    hh->m_polarity = HTTPType::REQUEST;

    // Make sure the line is not longer than max_request_line_size
    if (scanner->get_buffered_line_size() > max_request_line_size) {
      return ParseResult::ERROR;
    }

    text.assign(*start, real_end);
    err    = scanner->get(text, parsed, line_is_real, eof, MIMEScanner::ScanType::LINE);
    *start = text.data();
    if (static_cast<int>(err) < 0) {
      return err;
    }
    // We have to get a request line.  If we get parse done here,
    //   that meas we got an empty request
    if (err == ParseResult::DONE) {
      return ParseResult::ERROR;
    }
    if (err == ParseResult::CONT) {
      return err;
    }

    ink_assert(parsed.size() < UINT16_MAX);
    line_start = cur = parsed.data();
    end              = parsed.data_end();

    if (static_cast<unsigned>(end - line_start) > max_request_line_size) {
      return ParseResult::ERROR;
    }

    must_copy_strings = (must_copy_strings || (!line_is_real));

#if (ENABLE_PARSER_FAST_PATHS)
    // first try fast path
    if (end - cur >= 16) {
      if (((cur[0] ^ 'G') | (cur[1] ^ 'E') | (cur[2] ^ 'T')) != 0) {
        goto slow_case;
      }
      if (((end[-10] ^ 'H') | (end[-9] ^ 'T') | (end[-8] ^ 'T') | (end[-7] ^ 'P') | (end[-6] ^ '/') | (end[-4] ^ '.') |
           (end[-2] ^ '\r') | (end[-1] ^ '\n')) != 0) {
        goto slow_case;
      }
      if (!(isdigit(end[-5]) && isdigit(end[-3]))) {
        goto slow_case;
      }
      if (!(ParseRules::is_space(cur[3]) && (!ParseRules::is_space(cur[4])) && (!ParseRules::is_space(end[-12])) &&
            ParseRules::is_space(end[-11]))) {
        goto slow_case;
      }
      if (&(cur[4]) >= &(end[-11])) {
        goto slow_case;
      }

      HTTPVersion version{static_cast<uint8_t>(end[-5] - '0'), static_cast<uint8_t>(end[-3] - '0')};

      http_hdr_method_set(heap, hh, {&(cur[0]), 3}, HTTP_WKSIDX_GET, must_copy_strings);
      ink_assert(hh->u.req.m_url_impl != nullptr);
      url       = hh->u.req.m_url_impl;
      url_start = &(cur[4]);
      err       = ::url_parse(heap, url, &url_start, &(end[-11]), must_copy_strings, strict_uri_parsing);
      if (static_cast<int>(err) < 0) {
        return err;
      }
      if (!http_hdr_version_set(hh, version)) {
        return ParseResult::ERROR;
      }

      end                    = real_end;
      parser->m_parsing_http = false;

      ParseResult ret = mime_parser_parse(&parser->m_mime_parser, heap, hh->m_fields_impl, start, end, must_copy_strings, eof,
                                          false, max_hdr_field_size);
      // If we're done with the main parse do some validation
      if (ret == ParseResult::DONE) {
        ret = validate_hdr_request_target(HTTP_WKSIDX_GET, url);
      }
      if (ret == ParseResult::DONE) {
        ret = validate_hdr_host(hh); // check HOST header
      }
      if (ret == ParseResult::DONE) {
        ret = validate_hdr_content_length(heap, hh);
      }
      return ret;
    }
#endif

  slow_case:

    method_start  = nullptr;
    method_end    = nullptr;
    url_start     = nullptr;
    url_end       = nullptr;
    version_start = nullptr;
    version_end   = nullptr;
    url           = nullptr;

    if (ParseRules::is_cr(*cur))
      GETNEXT(done);
    if (ParseRules::is_lf(*cur)) {
      goto start;
    }

  parse_method1:

    if (ParseRules::is_ws(*cur)) {
      GETNEXT(done);
      goto parse_method1;
    }
    if (!ParseRules::is_token(*cur)) {
      goto done;
    }
    method_start = cur;
    GETNEXT(done);
  parse_method2:
    if (ParseRules::is_ws(*cur)) {
      method_end = cur;
      goto parse_version1;
    }
    if (!ParseRules::is_token(*cur)) {
      goto done;
    }
    GETNEXT(done);
    goto parse_method2;

  parse_version1:
    cur = end - 1;
    if (ParseRules::is_lf(*cur) && (cur >= line_start)) {
      cur -= 1;
    }
    if (ParseRules::is_cr(*cur) && (cur >= line_start)) {
      cur -= 1;
    }
    // A client may add extra white spaces after the HTTP version.
    // So, skip white spaces.
    while (ParseRules::is_ws(*cur) && (cur >= line_start)) {
      cur -= 1;
    }
    version_end = cur + 1;
  parse_version2:
    if (isdigit(*cur)) {
      GETPREV(parse_url);
      goto parse_version2;
    }
    if (*cur == '.') {
      GETPREV(parse_url);
      goto parse_version3;
    }
    goto parse_url;
  parse_version3:
    if (isdigit(*cur)) {
      GETPREV(parse_url);
      goto parse_version3;
    }
    if (*cur == '/') {
      GETPREV(parse_url);
      goto parse_version4;
    }
    goto parse_url;
  parse_version4:
    if (*cur != 'P') {
      goto parse_url;
    }
    GETPREV(parse_url);
    if (*cur != 'T') {
      goto parse_url;
    }
    GETPREV(parse_url);
    if (*cur != 'T') {
      goto parse_url;
    }
    GETPREV(parse_url);
    if (*cur != 'H') {
      goto parse_url;
    }
    version_start = cur;

  parse_url:
    url_start = method_end + 1;
    if (version_start) {
      url_end = version_start - 1;
    } else {
      url_end = end - 1;
    }
    while ((url_start < end) && ParseRules::is_ws(*url_start)) {
      url_start += 1;
    }
    while ((url_end >= line_start) && ParseRules::is_wslfcr(*url_end)) {
      url_end -= 1;
    }
    url_end += 1;

  done:
    if (!method_start || !method_end) {
      return ParseResult::ERROR;
    }

    // checking these with an if statement makes coverity flag as dead code because
    // url_start and url_end logically cannot be 0 at this time
    ink_assert(url_start);
    ink_assert(url_end);

    int method_wks_idx = hdrtoken_method_tokenize(method_start, static_cast<int>(method_end - method_start));
    http_hdr_method_set(heap, hh, {method_start, static_cast<std::string_view::size_type>(method_end - method_start)},
                        method_wks_idx, must_copy_strings);

    ink_assert(hh->u.req.m_url_impl != nullptr);

    url = hh->u.req.m_url_impl;
    err = ::url_parse(heap, url, &url_start, url_end, must_copy_strings, strict_uri_parsing);

    if (static_cast<int>(err) < 0) {
      return err;
    }

    HTTPVersion version;
    if (version_start && version_end) {
      version = http_parse_version(version_start, version_end);
    } else {
      return ParseResult::ERROR;
    }

    if (!http_hdr_version_set(hh, version)) {
      return ParseResult::ERROR;
    }

    end                    = real_end;
    parser->m_parsing_http = false;
  }

  ParseResult ret = mime_parser_parse(&parser->m_mime_parser, heap, hh->m_fields_impl, start, end, must_copy_strings, eof, false,
                                      max_hdr_field_size);
  // If we're done with the main parse do some validation
  if (ret == ParseResult::DONE) {
    ret = validate_hdr_request_target(hh->u.req.m_method_wks_idx, hh->u.req.m_url_impl);
  }
  if (ret == ParseResult::DONE) {
    ret = validate_hdr_host(hh); // check HOST header
  }
  if (ret == ParseResult::DONE) {
    ret = validate_hdr_content_length(heap, hh);
  }
  return ret;
}

ParseResult
validate_hdr_request_target(int method_wk_idx, URLImpl *url)
{
  ParseResult ret = ParseResult::DONE;
  auto        host{url->get_host()};
  auto        path{url->get_path()};
  auto        scheme{url->get_scheme()};

  if (host.empty()) {
    if (path == "*"sv) { // asterisk-form
      // Skip this check for now because URLImpl can't distinguish '*' and '/*'
      // if (method_wk_idx != HTTP_WKSIDX_OPTIONS) {
      //   ret = ParseResult::ERROR;
      // }
    } else { // origin-form
      // Nothing to check here
    }
  } else if (scheme.empty() && !host.empty()) { // authority-form
    if (method_wk_idx != HTTP_WKSIDX_CONNECT) {
      ret = ParseResult::ERROR;
    }
  } else { // absolute-form
    // Nothing to check here
  }

  return ret;
}

ParseResult
validate_hdr_host(HTTPHdrImpl *hh)
{
  ParseResult ret        = ParseResult::DONE;
  MIMEField  *host_field = mime_hdr_field_find(hh->m_fields_impl, static_cast<std::string_view>(MIME_FIELD_HOST));
  if (host_field) {
    if (host_field->has_dups()) {
      ret = ParseResult::ERROR; // can't have more than 1 host field.
    } else {
      auto             host{host_field->value_get()};
      std::string_view addr, port, rest;
      if (0 == ats_ip_parse(host, &addr, &port, &rest)) {
        if (!port.empty()) {
          if (port.size() > 5) {
            return ParseResult::ERROR;
          }
          int port_i = ink_atoi(port.data(), port.size());
          if (port_i >= 65536 || port_i <= 0) {
            return ParseResult::ERROR;
          }
        }
        if (!validate_host_name(addr)) {
          return ParseResult::ERROR;
        }
        if (ParseResult::DONE == ret && !std::all_of(rest.begin(), rest.end(), &ParseRules::is_ws)) {
          return ParseResult::ERROR;
        }
      } else {
        ret = ParseResult::ERROR;
      }
    }
  }
  return ret;
}

ParseResult
validate_hdr_content_length(HdrHeap *heap, HTTPHdrImpl *hh)
{
  MIMEField *content_length_field =
    mime_hdr_field_find(hh->m_fields_impl, static_cast<std::string_view>(MIME_FIELD_CONTENT_LENGTH));

  if (content_length_field) {
    // RFC 7230 section 3.3.3:
    // If a message is received with both a Transfer-Encoding and a
    // Content-Length header field, the Transfer-Encoding overrides
    // the Content-Length
    if (mime_hdr_field_find(hh->m_fields_impl, static_cast<std::string_view>(MIME_FIELD_TRANSFER_ENCODING)) != nullptr) {
      // Delete all Content-Length headers
      Dbg(dbg_ctl_http, "Transfer-Encoding header and Content-Length headers the request, removing all Content-Length headers");
      mime_hdr_field_delete(heap, hh->m_fields_impl, content_length_field, true);
      return ParseResult::DONE;
    }

    // RFC 7230 section 3.3.3:
    // If a message is received without Transfer-Encoding and with
    // either multiple Content-Length header fields having differing
    // field-values or a single Content-Length header field having an
    // invalid value, then the message framing is invalid and the
    // recipient MUST treat it as an unrecoverable error.  If this is a
    // request message, the server MUST respond with a 400 (Bad Request)
    // status code and then close the connection
    std::string_view value = content_length_field->value_get();

    // RFC 9110 section 8.6.
    // Content-Length = 1*DIGIT
    //
    if (value.empty()) {
      Dbg(dbg_ctl_http, "Content-Length headers don't match the ABNF, returning parse error");
      return ParseResult::ERROR;
    }

    // If the content-length value contains a non-numeric value, the header is invalid
    if (std::find_if(value.cbegin(), value.cend(), [](std::string_view::value_type c) { return !std::isdigit(c); }) !=
        value.cend()) {
      Dbg(dbg_ctl_http, "Content-Length value contains non-digit, returning parse error");
      return ParseResult::ERROR;
    }

    while (content_length_field->has_dups()) {
      std::string_view value_dup = content_length_field->m_next_dup->value_get();

      if ((value.length() != value_dup.length()) || value.compare(value_dup) != 0) {
        // Values are different, parse error
        Dbg(dbg_ctl_http, "Content-Length headers don't match, returning parse error");
        return ParseResult::ERROR;
      } else {
        // Delete the duplicate since it has the same value
        Dbg(dbg_ctl_http, "Deleting duplicate Content-Length header");
        mime_hdr_field_delete(heap, hh->m_fields_impl, content_length_field->m_next_dup, false);
      }
    }
  }

  return ParseResult::DONE;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

ParseResult
http_parser_parse_resp(HTTPParser *parser, HdrHeap *heap, HTTPHdrImpl *hh, const char **start, const char *end,
                       bool must_copy_strings, bool eof)
{
  if (parser->m_parsing_http) {
    MIMEScanner *scanner = &parser->m_mime_parser.m_scanner;

    ParseResult err;
    bool        line_is_real;
    const char *cur;
    const char *line_start;
    const char *real_end;
    const char *version_start;
    const char *version_end;
    const char *status_start;
    const char *status_end;
    const char *reason_start;
    const char *reason_end;
    const char *old_start;

    real_end  = end;
    old_start = *start;

    hh->m_polarity = HTTPType::RESPONSE;

    // Make sure the line is not longer than 64K
    if (scanner->get_buffered_line_size() >= UINT16_MAX) {
      return ParseResult::ERROR;
    }

    swoc::TextView text{*start, real_end};
    swoc::TextView parsed;
    err    = scanner->get(text, parsed, line_is_real, eof, MIMEScanner::ScanType::LINE);
    *start = text.data();
    if (static_cast<int>(err) < 0) {
      return err;
    }
    // Make sure the length headers are consistent
    if (err == ParseResult::DONE) {
      err = validate_hdr_content_length(heap, hh);
    }
    if ((err == ParseResult::DONE) || (err == ParseResult::CONT)) {
      return err;
    }

    ink_assert(parsed.size() < UINT16_MAX);
    line_start = cur = parsed.data();
    end              = parsed.data_end();

    must_copy_strings = (must_copy_strings || (!line_is_real));

#if (ENABLE_PARSER_FAST_PATHS)
    // first try fast path
    if (end - cur >= 16) {
      int http_match =
        ((cur[0] ^ 'H') | (cur[1] ^ 'T') | (cur[2] ^ 'T') | (cur[3] ^ 'P') | (cur[4] ^ '/') | (cur[6] ^ '.') | (cur[8] ^ ' '));
      if ((http_match != 0) || (!(isdigit(cur[5]) && isdigit(cur[7]) && isdigit(cur[9]) && isdigit(cur[10]) && isdigit(cur[11]) &&
                                  (!ParseRules::is_space(cur[13]))))) {
        goto slow_case;
      }

      reason_start = &(cur[13]);
      reason_end   = end - 1;
      while ((reason_end > reason_start + 1) && (ParseRules::is_space(reason_end[-1]))) {
        --reason_end;
      }

      HTTPVersion version(cur[5] - '0', cur[7] - '0');
      HTTPStatus  status = static_cast<HTTPStatus>((cur[9] - '0') * 100 + (cur[10] - '0') * 10 + (cur[11] - '0'));

      http_hdr_version_set(hh, version);
      http_hdr_status_set(hh, status);
      http_hdr_reason_set(heap, hh, {reason_start, static_cast<std::string_view::size_type>(reason_end - reason_start)},
                          must_copy_strings);

      end                    = real_end;
      parser->m_parsing_http = false;
      auto ret = mime_parser_parse(&parser->m_mime_parser, heap, hh->m_fields_impl, start, end, must_copy_strings, eof, true);
      // Make sure the length headers are consistent
      if (ret == ParseResult::DONE) {
        ret = validate_hdr_content_length(heap, hh);
      }
      return ret;
    }
#endif

  slow_case:
    version_start = nullptr;
    version_end   = nullptr;
    status_start  = nullptr;
    status_end    = nullptr;
    reason_start  = nullptr;
    reason_end    = nullptr;

    version_start = cur = line_start;
    if (*cur != 'H') {
      goto eoh;
    }
    GETNEXT(eoh);
    if (*cur != 'T') {
      goto eoh;
    }
    GETNEXT(eoh);
    if (*cur != 'T') {
      goto eoh;
    }
    GETNEXT(eoh);
    if (*cur != 'P') {
      goto eoh;
    }
    GETNEXT(eoh);
    if (*cur != '/') {
      goto eoh;
    }
    GETNEXT(eoh);
  parse_version2:
    if (isdigit(*cur)) {
      GETNEXT(eoh);
      goto parse_version2;
    }
    if (*cur == '.') {
      GETNEXT(eoh);
      goto parse_version3;
    }
    goto eoh;
  parse_version3:
    if (isdigit(*cur)) {
      GETNEXT(eoh);
      goto parse_version3;
    }
    if (ParseRules::is_ws(*cur)) {
      version_end = cur;
      GETNEXT(eoh);
      goto parse_status1;
    }
    goto eoh;

  parse_status1:
    if (ParseRules::is_ws(*cur)) {
      GETNEXT(done);
      goto parse_status1;
    }
    status_start = cur;
  parse_status2:
    status_end = cur;
    if (isdigit(*cur)) {
      GETNEXT(done);
      goto parse_status2;
    }
    if (ParseRules::is_ws(*cur)) {
      GETNEXT(done);
      goto parse_reason1;
    }
    goto done;

  parse_reason1:
    if (ParseRules::is_ws(*cur)) {
      GETNEXT(done);
      goto parse_reason1;
    }
    reason_start = cur;
    reason_end   = end - 1;
    while ((reason_end >= line_start) && (ParseRules::is_cr(*reason_end) || ParseRules::is_lf(*reason_end))) {
      reason_end -= 1;
    }
    reason_end += 1;
    goto done;

  eoh:
    *start = old_start;
    return ParseResult::ERROR; // This used to return ParseResult::DONE by default before

  done:
    if (!version_start || !version_end) {
      return ParseResult::ERROR;
    }

    HTTPVersion version = http_parse_version(version_start, version_end);

    if (version == HTTP_0_9) {
      return ParseResult::ERROR;
    }

    http_hdr_version_set(hh, version);

    if (status_start && status_end) {
      http_hdr_status_set(hh, http_parse_status(status_start, status_end));
    }

    if (reason_start && reason_end) {
      http_hdr_reason_set(heap, hh, {reason_start, static_cast<std::string_view::size_type>(reason_end - reason_start)},
                          must_copy_strings);
    }

    end                    = real_end;
    parser->m_parsing_http = false;
  }
  auto ret = mime_parser_parse(&parser->m_mime_parser, heap, hh->m_fields_impl, start, end, must_copy_strings, eof, true);
  // Make sure the length headers are consistent
  if (ret == ParseResult::DONE) {
    ret = validate_hdr_content_length(heap, hh);
  }
  return ret;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

HTTPStatus
http_parse_status(const char *start, const char *end)
{
  int status = 0;

  while ((start != end) && ParseRules::is_space(*start)) {
    start += 1;
  }

  while ((start != end) && isdigit(*start)) {
    status = (status * 10) + (*start++ - '0');
  }

  return static_cast<HTTPStatus>(status);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

HTTPVersion
http_parse_version(const char *start, const char *end)
{
  int maj;
  int min;

  if ((end - start) < 8) {
    return HTTP_0_9;
  }

  if ((start[0] == 'H') && (start[1] == 'T') && (start[2] == 'T') && (start[3] == 'P') && (start[4] == '/')) {
    start += 5;

    maj = 0;
    min = 0;

    while ((start != end) && isdigit(*start)) {
      maj    = (maj * 10) + (*start - '0');
      start += 1;
    }

    if (*start == '.') {
      start += 1;
    }

    while ((start != end) && isdigit(*start)) {
      min    = (min * 10) + (*start - '0');
      start += 1;
    }

    return HTTPVersion(maj, min);
  }

  return HTTP_0_9;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static char *
http_str_store(Arena *arena, const char *str, int length)
{
  const char *wks;
  int         idx = hdrtoken_tokenize(str, length, &wks);
  if (idx < 0) {
    return arena->str_store(str, length);
  } else {
    return const_cast<char *>(wks);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static void
http_skip_ws(const char *&buf, int &len)
{
  while (len > 0 && *buf && ParseRules::is_ws(*buf)) {
    buf += 1;
    len -= 1;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static double
http_parse_qvalue(const char *&buf, int &len)
{
  double val = 1.0;

  if (*buf != ';') {
    return val;
  }

  buf += 1;
  len -= 1;

  while (len > 0 && *buf) {
    http_skip_ws(buf, len);

    if (*buf == 'q') {
      buf += 1;
      len -= 1;
      http_skip_ws(buf, len);

      if (*buf == '=') {
        double n;
        int    f;

        buf += 1;
        len -= 1;
        http_skip_ws(buf, len);

        n = 0.0;
        while (len > 0 && *buf && isdigit(*buf)) {
          n    = (n * 10) + (*buf++ - '0');
          len -= 1;
        }

        if (*buf == '.') {
          buf += 1;
          len -= 1;

          f = 10;
          while (len > 0 && *buf && isdigit(*buf)) {
            n   += (*buf++ - '0') / static_cast<double>(f);
            f   *= 10;
            len -= 1;
          }
        }

        val = n;
      }
    } else {
      // The current parameter is not a q-value, so go to the next param.
      while (len > 0 && *buf) {
        if (*buf != ';') {
          buf += 1;
          len -= 1;
        } else {
          // Move to the character after the semicolon.
          buf += 1;
          len -= 1;
          break;
        }
      }
    }
  }

  return val;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
  TE        = "TE" ":" #( t-codings )
  t-codings = "trailers" | ( transfer-extension [ accept-params ] )
  -------------------------------------------------------------------------*/

HTTPValTE *
http_parse_te(const char *buf, int len, Arena *arena)
{
  HTTPValTE  *val;
  const char *s;

  http_skip_ws(buf, len);

  s = buf;

  while (len > 0 && *buf && (*buf != ';')) {
    buf += 1;
    len -= 1;
  }

  val           = static_cast<HTTPValTE *>(arena->alloc(sizeof(HTTPValTE)));
  val->encoding = http_str_store(arena, s, static_cast<int>(buf - s));
  val->qvalue   = http_parse_qvalue(buf, len);

  return val;
}

void
HTTPHdr::_fill_target_cache() const
{
  URL *url = this->url_get();

  m_target_in_url  = false;
  m_port_in_header = false;
  m_host_mime      = nullptr;
  // Check in the URL first, then the HOST field.
  std::string_view host;
  if (host = url->host_get(); nullptr != host.data()) {
    m_target_in_url  = true;
    m_port           = url->port_get();
    m_port_in_header = 0 != url->port_get_raw();
    m_host_mime      = nullptr;
    m_host_length    = static_cast<int>(host.length());
  } else {
    std::string_view port;
    std::tie(m_host_mime, host, port) = const_cast<HTTPHdr *>(this)->get_host_port_values();
    m_host_length                     = static_cast<int>(host.length());

    if (m_host_mime != nullptr) {
      m_port = 0;
      if (!port.empty()) {
        for (auto c : port) {
          if (isdigit(c)) {
            m_port = m_port * 10 + c - '0';
          }
        }
      }
      m_port_in_header = (0 != m_port);
      m_port           = url_canonicalize_port(url->m_url_impl->m_url_type, m_port);
    }
  }

  m_target_cached = true;
}

void
HTTPHdr::set_url_target_from_host_field(URL *url)
{
  this->_test_and_fill_target_cache();

  if (!url) {
    // Use local cached URL and don't copy if the target
    // is already there.
    if (!m_target_in_url && m_host_mime && m_host_length) {
      m_url_cached.host_set({m_host_mime->m_ptr_value, static_cast<std::string_view::size_type>(m_host_length)});
      if (m_port_in_header) {
        m_url_cached.port_set(m_port);
      }
      m_target_in_url = true; // it's there now.
    }
  } else {
    url->host_set(host_get());
    if (m_port_in_header) {
      url->port_set(m_port);
    }
  }
}

// Very ugly, but a proper implementation will require
// rewriting the URL class and all of its clients so that
// clients access the URL through the HTTP header instance
// unless they really need low level access. The header would
// need to either keep two versions of the URL (pristine
// and effective) or URl would have to provide access to
// the URL printer.

/// Hack the URL in the HTTP header to be 1.0 compliant, saving the
/// original values so they can be restored.
class UrlPrintHack
{
  friend class HTTPHdr;
  UrlPrintHack(HTTPHdr *hdr)
  {
    hdr->_test_and_fill_target_cache();
    if (hdr->m_url_cached.valid()) {
      URLImpl *ui = hdr->m_url_cached.m_url_impl;

      m_hdr = hdr; // mark as potentially having modified values.

      /* Get dirty. We reach in to the URL implementation to
         set the host and port if
         1) They are not already set
         AND
         2) The values were in a HTTP header.
      */
      if (!hdr->m_target_in_url && hdr->m_host_length && hdr->m_host_mime) {
        ink_assert(nullptr == ui->m_ptr_host); // shouldn't be non-zero if not in URL.
        ui->m_ptr_host    = hdr->m_host_mime->m_ptr_value;
        ui->m_len_host    = hdr->m_host_length;
        m_host_modified_p = true;
      } else {
        m_host_modified_p = false;
      }

      if (0 == hdr->m_url_cached.port_get_raw() && hdr->m_port_in_header) {
        ink_assert(nullptr == ui->m_ptr_port); // shouldn't be set if not in URL.
        ui->m_ptr_port    = m_port_buff;
        ui->m_len_port    = snprintf(m_port_buff, sizeof(m_port_buff), "%d", hdr->m_port);
        ui->m_port        = hdr->m_port;
        m_port_modified_p = true;
      } else {
        m_port_modified_p = false;
      }
    } else {
      m_hdr = nullptr;
    }
  }

  /// Destructor.
  ~UrlPrintHack()
  {
    if (m_hdr) { // There was a potentially modified header.
      URLImpl *ui = m_hdr->m_url_cached.m_url_impl;
      // Because we only modified if not set, we can just set these values
      // back to zero if modified. We want to be careful because if a
      // heap re-allocation happened while this was active, then a saved value
      // is wrong and will break things if restored. We don't have to worry
      // about these because, if modified, they were originally NULL and should
      // still be NULL after a re-allocate.
      if (m_port_modified_p) {
        ui->m_len_port = 0;
        ui->m_ptr_port = nullptr;
        ui->m_port     = 0;
      }
      if (m_host_modified_p) {
        ui->m_len_host = 0;
        ui->m_ptr_host = nullptr;
      }
    }
  }

  /// Check if the hack worked
  bool
  is_valid() const
  {
    return nullptr != m_hdr;
  }

  /// Saved values.
  ///@{
  bool     m_host_modified_p = false;
  bool     m_port_modified_p = false;
  HTTPHdr *m_hdr             = nullptr;
  ///@}
  /// Temporary buffer for port data.
  char m_port_buff[32];
};

char *
HTTPHdr::url_string_get(Arena *arena, int *length)
{
  char        *zret = nullptr;
  UrlPrintHack hack(this);

  if (hack.is_valid()) {
    // The use of a magic value for Arena to indicate the internal heap is
    // even uglier but it's less so than duplicating this entire method to
    // change that one thing.

    zret = (arena == USE_HDR_HEAP_MAGIC) ? m_url_cached.string_get_ref(length) : m_url_cached.string_get(arena, length);
  }
  return zret;
}

int
HTTPHdr::url_print(char *buff, int length, int *offset, int *skip, unsigned normalization_flags)
{
  ink_release_assert(offset);
  ink_release_assert(skip);

  int          zret = 0;
  UrlPrintHack hack(this);
  if (hack.is_valid()) {
    zret = m_url_cached.print(buff, length, offset, skip, normalization_flags);
  }
  return zret;
}

int
HTTPHdr::url_printed_length(unsigned normalization_flags)
{
  int          zret = -1;
  UrlPrintHack hack(this);
  if (hack.is_valid()) {
    zret = m_url_cached.length_get(normalization_flags);
  }
  return zret;
}

// Look for headers that the proxy will need to be able to process
// Return false if the proxy does not know how to process the header
// Currently just looking at TRANSFER_ENCODING.  The proxy only knows how to
// process the chunked action
bool
HTTPHdr::check_hdr_implements()
{
  bool       retval = true;
  MIMEField *transfer_encode =
    mime_hdr_field_find(this->m_http->m_fields_impl, static_cast<std::string_view>(MIME_FIELD_TRANSFER_ENCODING));
  if (transfer_encode) {
    do {
      auto val{transfer_encode->value_get()};
      if (val.length() != 7 || 0 != strncasecmp(val.data(), "chunked", val.length())) {
        retval = false;
      }
      transfer_encode = transfer_encode->m_next_dup;
    } while (retval && transfer_encode);
  }
  return retval;
}

/***********************************************************************
 *                                                                     *
 *                        M A R S H A L I N G                          *
 *                                                                     *
 ***********************************************************************/

int
HTTPHdr::unmarshal(char *buf, int len, RefCountObj *block_ref)
{
  m_heap = reinterpret_cast<HdrHeap *>(buf);

  int res =
    m_heap->unmarshal(len, static_cast<int>(HdrHeapObjType::HTTP_HEADER), reinterpret_cast<HdrHeapObjImpl **>(&m_http), block_ref);

  if (res > 0) {
    m_mime = m_http->m_fields_impl;
  } else {
    clear();
  }

  return res;
}

int
HTTPHdrImpl::marshal(MarshalXlate *ptr_xlate, int num_ptr, MarshalXlate *str_xlate, int num_str)
{
  if (m_polarity == HTTPType::REQUEST) {
    HDR_MARSHAL_STR(u.req.m_ptr_method, str_xlate, num_str);
    HDR_MARSHAL_PTR(u.req.m_url_impl, URLImpl, ptr_xlate, num_ptr);
  } else if (m_polarity == HTTPType::RESPONSE) {
    HDR_MARSHAL_STR(u.resp.m_ptr_reason, str_xlate, num_str);
  } else {
    ink_release_assert(!"unknown m_polarity");
  }

  HDR_MARSHAL_PTR(m_fields_impl, MIMEHdrImpl, ptr_xlate, num_ptr);

  return 0;
}

void
HTTPHdrImpl::unmarshal(intptr_t offset)
{
  if (m_polarity == HTTPType::REQUEST) {
    HDR_UNMARSHAL_STR(u.req.m_ptr_method, offset);
    HDR_UNMARSHAL_PTR(u.req.m_url_impl, URLImpl, offset);
  } else if (m_polarity == HTTPType::RESPONSE) {
    HDR_UNMARSHAL_STR(u.resp.m_ptr_reason, offset);
  } else {
    ink_release_assert(!"unknown m_polarity");
  }

  HDR_UNMARSHAL_PTR(m_fields_impl, MIMEHdrImpl, offset);
}

void
HTTPHdrImpl::move_strings(HdrStrHeap *new_heap)
{
  if (m_polarity == HTTPType::REQUEST) {
    HDR_MOVE_STR(u.req.m_ptr_method, u.req.m_len_method);
  } else if (m_polarity == HTTPType::RESPONSE) {
    HDR_MOVE_STR(u.resp.m_ptr_reason, u.resp.m_len_reason);
  } else {
    ink_release_assert(!"unknown m_polarity");
  }
}

size_t
HTTPHdrImpl::strings_length()
{
  size_t ret = 0;

  if (m_polarity == HTTPType::REQUEST) {
    ret += u.req.m_len_method;
  } else if (m_polarity == HTTPType::RESPONSE) {
    ret += u.resp.m_len_reason;
  }
  return ret;
}

void
HTTPHdrImpl::check_strings(HeapCheck *heaps, int num_heaps)
{
  if (m_polarity == HTTPType::REQUEST) {
    CHECK_STR(u.req.m_ptr_method, u.req.m_len_method, heaps, num_heaps);
  } else if (m_polarity == HTTPType::RESPONSE) {
    CHECK_STR(u.resp.m_ptr_reason, u.resp.m_len_reason, heaps, num_heaps);
  } else {
    ink_release_assert(!"unknown m_polarity");
  }
}

ClassAllocator<HTTPCacheAlt> httpCacheAltAllocator("httpCacheAltAllocator");

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
int constexpr HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS;

HTTPCacheAlt::HTTPCacheAlt() : m_request_hdr(), m_response_hdr()

{
  memset(&m_object_key[0], 0, CRYPTO_HASH_SIZE);
  m_object_size[0] = 0;
  m_object_size[1] = 0;
}

void
HTTPCacheAlt::destroy()
{
  ink_assert(m_magic == CacheAltMagic::ALIVE);
  ink_assert(m_writeable);
  m_magic     = CacheAltMagic::DEAD;
  m_writeable = 0;
  m_request_hdr.destroy();
  m_response_hdr.destroy();
  m_frag_offset_count = 0;
  if (m_frag_offsets && m_frag_offsets != m_integral_frag_offsets) {
    ats_free(m_frag_offsets);
    m_frag_offsets = nullptr;
  }
  httpCacheAltAllocator.free(this);
}

void
HTTPCacheAlt::copy(HTTPCacheAlt *to_copy)
{
  m_magic = to_copy->m_magic;
  // m_writeable =      to_copy->m_writeable;
  m_unmarshal_len = to_copy->m_unmarshal_len;
  m_id            = to_copy->m_id;
  m_rid           = to_copy->m_rid;
  memcpy(&m_object_key[0], &to_copy->m_object_key[0], CRYPTO_HASH_SIZE);
  m_object_size[0] = to_copy->m_object_size[0];
  m_object_size[1] = to_copy->m_object_size[1];

  if (to_copy->m_request_hdr.valid()) {
    m_request_hdr.copy(&to_copy->m_request_hdr);
  }

  if (to_copy->m_response_hdr.valid()) {
    m_response_hdr.copy(&to_copy->m_response_hdr);
  }

  m_request_sent_time      = to_copy->m_request_sent_time;
  m_response_received_time = to_copy->m_response_received_time;
  this->copy_frag_offsets_from(to_copy);
}

void
HTTPCacheAlt::copy_frag_offsets_from(HTTPCacheAlt *src)
{
  m_frag_offset_count = src->m_frag_offset_count;
  if (m_frag_offset_count > 0) {
    if (m_frag_offset_count > N_INTEGRAL_FRAG_OFFSETS) {
      /* Mixed feelings about this - technically we don't need it to be a
         power of two when copied because currently that means it is frozen.
         But that could change later and it would be a nasty bug to find.
         So we'll do it for now. The relative overhead is tiny.
      */
      int bcount = HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS * 2;
      while (bcount < m_frag_offset_count) {
        bcount *= 2;
      }
      m_frag_offsets = static_cast<FragOffset *>(ats_malloc(sizeof(FragOffset) * bcount));
    } else {
      m_frag_offsets = m_integral_frag_offsets;
    }
    memcpy(m_frag_offsets, src->m_frag_offsets, sizeof(FragOffset) * m_frag_offset_count);
  }
}

const int HTTP_ALT_MARSHAL_SIZE = HdrHeapMarshalBlocks{swoc::round_up(sizeof(HTTPCacheAlt))};

void
HTTPInfo::create()
{
  m_alt = httpCacheAltAllocator.alloc();
}

void
HTTPInfo::copy(HTTPInfo *hi)
{
  if (m_alt && m_alt->m_writeable) {
    destroy();
  }

  create();
  m_alt->copy(hi->m_alt);
}

void
HTTPInfo::copy_frag_offsets_from(HTTPInfo *src)
{
  if (m_alt && src->m_alt) {
    m_alt->copy_frag_offsets_from(src->m_alt);
  }
}

int
HTTPInfo::marshal_length()
{
  int len = HTTP_ALT_MARSHAL_SIZE;

  if (m_alt->m_request_hdr.valid()) {
    len += m_alt->m_request_hdr.m_heap->marshal_length();
  }

  if (m_alt->m_response_hdr.valid()) {
    len += m_alt->m_response_hdr.m_heap->marshal_length();
  }

  if (m_alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    len += sizeof(FragOffset) * m_alt->m_frag_offset_count;
  }

  return len;
}

int
HTTPInfo::marshal(char *buf, int len)
{
  int           tmp;
  int           used        = 0;
  HTTPCacheAlt *marshal_alt = reinterpret_cast<HTTPCacheAlt *>(buf);
  // non-zero only if the offsets are external. Otherwise they get
  // marshalled along with the alt struct.
  ink_assert(m_alt->m_magic == CacheAltMagic::ALIVE);

  // Make sure the buffer is aligned
  //    ink_assert(((intptr_t)buf) & 0x3 == 0);

  // Memcpy the whole object so that we can use it
  //   live later.  This involves copying a few
  //   extra bytes now but will save copying any
  //   bytes on the way out of the cache
  memcpy(buf, m_alt, sizeof(HTTPCacheAlt));
  marshal_alt->m_magic          = CacheAltMagic::MARSHALED;
  marshal_alt->m_writeable      = 0;
  marshal_alt->m_unmarshal_len  = -1;
  marshal_alt->m_ext_buffer     = nullptr;
  buf                          += HTTP_ALT_MARSHAL_SIZE;
  used                         += HTTP_ALT_MARSHAL_SIZE;

  if (m_alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    marshal_alt->m_frag_offsets = static_cast<FragOffset *>(reinterpret_cast<void *>(used));
    memcpy(buf, m_alt->m_frag_offsets, m_alt->m_frag_offset_count * sizeof(FragOffset));
    buf  += m_alt->m_frag_offset_count * sizeof(FragOffset);
    used += m_alt->m_frag_offset_count * sizeof(FragOffset);
  } else {
    marshal_alt->m_frag_offsets = nullptr;
  }

  // The m_{request,response}_hdr->m_heap pointers are converted
  //    to zero based offsets from the start of the buffer we're
  //    marshalling in to
  if (m_alt->m_request_hdr.valid()) {
    tmp                               = m_alt->m_request_hdr.m_heap->marshal(buf, len - used);
    marshal_alt->m_request_hdr.m_heap = (HdrHeap *)static_cast<intptr_t>(used);
    ink_assert(((intptr_t)marshal_alt->m_request_hdr.m_heap) < len);
    buf  += tmp;
    used += tmp;
  } else {
    marshal_alt->m_request_hdr.m_heap = nullptr;
  }

  if (m_alt->m_response_hdr.valid()) {
    tmp                                = m_alt->m_response_hdr.m_heap->marshal(buf, len - used);
    marshal_alt->m_response_hdr.m_heap = (HdrHeap *)static_cast<intptr_t>(used);
    ink_assert(((intptr_t)marshal_alt->m_response_hdr.m_heap) < len);
    used += tmp;
  } else {
    marshal_alt->m_response_hdr.m_heap = nullptr;
  }

  // The prior system failed the marshal if there wasn't
  //   enough space by measuring the space for every
  //   component. Seems much faster to check once to
  //   see if we spammed memory
  ink_release_assert(used <= len);

  return used;
}

int
HTTPInfo::unmarshal(char *buf, int len, RefCountObj *block_ref)
{
  HTTPCacheAlt *alt      = reinterpret_cast<HTTPCacheAlt *>(buf);
  int           orig_len = len;

  if (alt->m_magic == CacheAltMagic::ALIVE) {
    // Already unmarshaled, must be a ram cache
    //  it
    ink_assert(alt->m_unmarshal_len > 0);
    ink_assert(alt->m_unmarshal_len <= len);
    return alt->m_unmarshal_len;
  } else if (alt->m_magic != CacheAltMagic::MARSHALED) {
    ink_assert(!"HTTPInfo::unmarshal bad magic");
    return -1;
  }

  ink_assert(alt->m_unmarshal_len < 0);
  alt->m_magic = CacheAltMagic::ALIVE;
  ink_assert(alt->m_writeable == 0);
  len -= HTTP_ALT_MARSHAL_SIZE;

  if (alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    alt->m_frag_offsets  = reinterpret_cast<FragOffset *>(buf + reinterpret_cast<intptr_t>(alt->m_frag_offsets));
    len                 -= sizeof(FragOffset) * alt->m_frag_offset_count;
    ink_assert(len >= 0);
  } else if (alt->m_frag_offset_count > 0) {
    alt->m_frag_offsets = alt->m_integral_frag_offsets;
  } else {
    alt->m_frag_offsets = nullptr; // should really already be zero.
  }

  HdrHeap *heap   = reinterpret_cast<HdrHeap *>(alt->m_request_hdr.m_heap ? (buf + (intptr_t)alt->m_request_hdr.m_heap) : nullptr);
  HTTPHdrImpl *hh = nullptr;
  int          tmp;
  if (heap != nullptr) {
    tmp = heap->unmarshal(len, static_cast<int>(HdrHeapObjType::HTTP_HEADER), reinterpret_cast<HdrHeapObjImpl **>(&hh), block_ref);
    if (hh == nullptr || tmp < 0) {
      ink_assert(!"HTTPInfo::request unmarshal failed");
      return -1;
    }
    len                                    -= tmp;
    alt->m_request_hdr.m_heap               = heap;
    alt->m_request_hdr.m_http               = hh;
    alt->m_request_hdr.m_mime               = hh->m_fields_impl;
    alt->m_request_hdr.m_url_cached.m_heap  = heap;
  }

  heap = reinterpret_cast<HdrHeap *>(alt->m_response_hdr.m_heap ? (buf + (intptr_t)alt->m_response_hdr.m_heap) : nullptr);
  if (heap != nullptr) {
    tmp = heap->unmarshal(len, static_cast<int>(HdrHeapObjType::HTTP_HEADER), reinterpret_cast<HdrHeapObjImpl **>(&hh), block_ref);
    if (hh == nullptr || tmp < 0) {
      ink_assert(!"HTTPInfo::response unmarshal failed");
      return -1;
    }
    len -= tmp;

    alt->m_response_hdr.m_heap = heap;
    alt->m_response_hdr.m_http = hh;
    alt->m_response_hdr.m_mime = hh->m_fields_impl;
  }

  alt->m_unmarshal_len = orig_len - len;

  return alt->m_unmarshal_len;
}

int
HTTPInfo::unmarshal_v24_1(char *buf, int len, RefCountObj *block_ref)
{
  HTTPCacheAlt *alt      = reinterpret_cast<HTTPCacheAlt *>(buf);
  int           orig_len = len;

  if (alt->m_magic == CacheAltMagic::ALIVE) {
    // Already unmarshaled, must be a ram cache
    //  it
    ink_assert(alt->m_unmarshal_len > 0);
    ink_assert(alt->m_unmarshal_len <= len);
    return alt->m_unmarshal_len;
  } else if (alt->m_magic != CacheAltMagic::MARSHALED) {
    ink_assert(!"HTTPInfo::unmarshal bad magic");
    return -1;
  }

  ink_assert(alt->m_unmarshal_len < 0);
  alt->m_magic = CacheAltMagic::ALIVE;
  ink_assert(alt->m_writeable == 0);
  len -= HTTP_ALT_MARSHAL_SIZE;

  if (alt->m_frag_offset_count > HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS) {
    // stuff that didn't fit in the integral slots.
    int   extra     = sizeof(FragOffset) * alt->m_frag_offset_count - sizeof(alt->m_integral_frag_offsets);
    char *extra_src = buf + reinterpret_cast<intptr_t>(alt->m_frag_offsets);
    // Actual buffer size, which must be a power of two.
    // Well, technically not, because we never modify an unmarshalled fragment
    // offset table, but it would be a nasty bug should that be done in the
    // future.
    int bcount = HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS * 2;

    while (bcount < alt->m_frag_offset_count) {
      bcount *= 2;
    }
    alt->m_frag_offsets =
      static_cast<FragOffset *>(ats_malloc(bcount * sizeof(FragOffset))); // WRONG - must round up to next power of 2.
    memcpy(alt->m_frag_offsets, alt->m_integral_frag_offsets, sizeof(alt->m_integral_frag_offsets));
    memcpy(alt->m_frag_offsets + HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS, extra_src, extra);
    len -= extra;
  } else if (alt->m_frag_offset_count > 0) {
    alt->m_frag_offsets = alt->m_integral_frag_offsets;
  } else {
    alt->m_frag_offsets = nullptr; // should really already be zero.
  }

  HdrHeap *heap   = reinterpret_cast<HdrHeap *>(alt->m_request_hdr.m_heap ? (buf + (intptr_t)alt->m_request_hdr.m_heap) : nullptr);
  HTTPHdrImpl *hh = nullptr;
  int          tmp;
  if (heap != nullptr) {
    tmp = heap->unmarshal(len, static_cast<int>(HdrHeapObjType::HTTP_HEADER), reinterpret_cast<HdrHeapObjImpl **>(&hh), block_ref);
    if (hh == nullptr || tmp < 0) {
      ink_assert(!"HTTPInfo::request unmarshal failed");
      return -1;
    }
    len                                    -= tmp;
    alt->m_request_hdr.m_heap               = heap;
    alt->m_request_hdr.m_http               = hh;
    alt->m_request_hdr.m_mime               = hh->m_fields_impl;
    alt->m_request_hdr.m_url_cached.m_heap  = heap;
  }

  heap = reinterpret_cast<HdrHeap *>(alt->m_response_hdr.m_heap ? (buf + (intptr_t)alt->m_response_hdr.m_heap) : nullptr);
  if (heap != nullptr) {
    tmp = heap->unmarshal(len, static_cast<int>(HdrHeapObjType::HTTP_HEADER), reinterpret_cast<HdrHeapObjImpl **>(&hh), block_ref);
    if (hh == nullptr || tmp < 0) {
      ink_assert(!"HTTPInfo::response unmarshal failed");
      return -1;
    }
    len -= tmp;

    alt->m_response_hdr.m_heap = heap;
    alt->m_response_hdr.m_http = hh;
    alt->m_response_hdr.m_mime = hh->m_fields_impl;
  }

  alt->m_unmarshal_len = orig_len - len;

  return alt->m_unmarshal_len;
}

// bool HTTPInfo::check_marshalled(char* buf, int len)
//  Checks a marhshalled HTTPInfo buffer to make
//    sure it's sane.  Returns true if sane, false otherwise
//
bool
HTTPInfo::check_marshalled(char *buf, int len)
{
  HTTPCacheAlt *alt = reinterpret_cast<HTTPCacheAlt *>(buf);

  if (alt->m_magic != CacheAltMagic::MARSHALED) {
    return false;
  }

  if (alt->m_writeable != false) {
    return false;
  }

  if (len < HTTP_ALT_MARSHAL_SIZE) {
    return false;
  }

  if (alt->m_request_hdr.m_heap == nullptr) {
    return false;
  }

  if ((intptr_t)alt->m_request_hdr.m_heap > len) {
    return false;
  }

  HdrHeap *heap = reinterpret_cast<HdrHeap *>(buf + (intptr_t)alt->m_request_hdr.m_heap);
  if (heap->check_marshalled(len) == false) {
    return false;
  }

  if (alt->m_response_hdr.m_heap == nullptr) {
    return false;
  }

  if ((intptr_t)alt->m_response_hdr.m_heap > len) {
    return false;
  }

  heap = reinterpret_cast<HdrHeap *>(buf + (intptr_t)alt->m_response_hdr.m_heap);
  if (heap->check_marshalled(len) == false) {
    return false;
  }

  return true;
}

// void HTTPInfo::set_buffer_reference(RefCountObj* block_ref)
//
//    Setting a buffer reference for the alt is separate from
//     the unmarshalling operation because the clustering
//     utilizes the system differently than cache does
//    The cache maintains external refcounting of the buffer that
//     the alt is in & doesn't always destroy the alt when its
//     done with it because it figures it doesn't need to since
//     it is managing the buffer
//    The receiver of ClusterRPC system has the alt manage the
//     buffer itself and therefore needs to call this function
//     to set up the reference
//
void
HTTPInfo::set_buffer_reference(RefCountObj *block_ref)
{
  ink_assert(m_alt->m_magic == CacheAltMagic::ALIVE);

  // Free existing reference
  if (m_alt->m_ext_buffer != nullptr) {
    if (m_alt->m_ext_buffer->refcount_dec() == 0) {
      m_alt->m_ext_buffer->free();
    }
  }
  // Set up the ref count for the external buffer
  //   if there is one
  if (block_ref) {
    block_ref->refcount_inc();
  }

  m_alt->m_ext_buffer = block_ref;
}

int
HTTPInfo::get_handle(char *buf, int len)
{
  // All the offsets have already swizzled to pointers.  All we
  //  need to do is set m_alt and make sure things are sane
  HTTPCacheAlt *a = reinterpret_cast<HTTPCacheAlt *>(buf);

  if (a->m_magic == CacheAltMagic::ALIVE) {
    m_alt = a;
    ink_assert(m_alt->m_unmarshal_len > 0);
    ink_assert(m_alt->m_unmarshal_len <= len);
    return m_alt->m_unmarshal_len;
  }

  clear();
  return -1;
}

void
HTTPInfo::push_frag_offset(FragOffset offset)
{
  ink_assert(m_alt);
  if (nullptr == m_alt->m_frag_offsets) {
    m_alt->m_frag_offsets = m_alt->m_integral_frag_offsets;
  } else if (m_alt->m_frag_offset_count >= HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS &&
             0 == (m_alt->m_frag_offset_count & (m_alt->m_frag_offset_count - 1))) {
    // need more space than in integral storage and we're at an upgrade
    // size (power of 2).
    FragOffset *nf = static_cast<FragOffset *>(ats_malloc(sizeof(FragOffset) * (m_alt->m_frag_offset_count * 2)));
    memcpy(nf, m_alt->m_frag_offsets, sizeof(FragOffset) * m_alt->m_frag_offset_count);
    if (m_alt->m_frag_offsets != m_alt->m_integral_frag_offsets) {
      ats_free(m_alt->m_frag_offsets);
    }
    m_alt->m_frag_offsets = nf;
  }

  m_alt->m_frag_offsets[m_alt->m_frag_offset_count++] = offset;
}
