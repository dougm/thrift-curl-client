/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <cstdlib>
#include <sstream>

#include "TCurlClient.h"

/**
 * based on thrift/lib/cpp/src/transport/THttpClient.cpp
 * and      thrift/lib/perl/lib/Thrift/HttpClient.pm
 */
namespace apache { namespace thrift { namespace transport {

using namespace std;

#define CONTENT_TYPE "application/x-thrift"
#define USE_EXPECT
#define USE_CHUNKED
#define USE_KEEPALIVE

/* read HTTP response body */
size_t TCurlClient::curl_write(void *ptr, size_t size, size_t nmemb, void *data)
{
  uint32_t len = size*nmemb;
  TCurlClient *tcc = (TCurlClient *)data;

  tcc->readBuffer_.write((uint8_t *)ptr, len);
  return len;
}

/* read POST data */
size_t TCurlClient::curl_read(void *ptr, size_t size, size_t nmemb, void *data)
{
  uint32_t len = size*nmemb;
  TCurlClient *tcc = (TCurlClient *)data;
  uint32_t avail = tcc->writeBuffer_.available_read();

  if (avail < 0) {
    return 0;
  }
  else if (len > avail) {
    len = avail;
  }
  return tcc->writeBuffer_.read((uint8_t *)ptr, len);
}

TCurlClient::TCurlClient(string url) :
  url_(url) {
  isOpen_ = false;
}

TCurlClient::~TCurlClient() {
  cleanup();
}

void TCurlClient::open() {
#ifdef USE_KEEPALIVE
  init();
#endif
}

void TCurlClient::close() {
#ifdef USE_KEEPALIVE
  cleanup();
#endif
}

void TCurlClient::init() {
  curl_ = curl_easy_init();
  curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
  curl_easy_setopt(curl_, CURLOPT_USERAGENT, "C++/TCurlClient");
  curl_easy_setopt(curl_, CURLOPT_POST, 1L);
#ifdef USE_DEBUG
  curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);
#endif
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, TCurlClient::curl_write);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, (void *)this);

  curl_easy_setopt(curl_, CURLOPT_READFUNCTION, TCurlClient::curl_read);
  curl_easy_setopt(curl_, CURLOPT_READDATA, (void *)this);

  if (credentials_.length() > 0) {
    curl_easy_setopt(curl_, CURLOPT_USERPWD, credentials_.c_str());
  }

  headers_ = NULL;
#ifdef USE_CHUNKED
  headers_ = curl_slist_append(headers_, "Transfer-Encoding: chunked");
#endif
#ifndef USE_EXPECT
  headers_ = curl_slist_append(headers_, "Expect:");
#endif
  headers_ = curl_slist_append(headers_, "Content-Type: " CONTENT_TYPE);
  headers_ = curl_slist_append(headers_, "Accept: " CONTENT_TYPE);

  for (std::vector<std::string>::iterator hdr = xheaders_.begin();
       hdr != xheaders_.end(); ++hdr) {

    headers_ = curl_slist_append(headers_, (*hdr).c_str());
  }

  curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);

  for (std::map<int,std::string>::iterator opt = options_.begin();
       opt != options_.end(); opt++) {

    CURLoption key = (CURLoption)opt->first;
    if (key < CURLOPTTYPE_OBJECTPOINT) {
      long lval;
      if (opt->second.compare("true") == 0) {
        lval = 1;
      }
      else if (opt->second.compare("false") == 0) {
        lval = 0;
      }
      else {
        lval = strtol(opt->second.c_str(), NULL, 10);
      }
      curl_easy_setopt(curl_, key, lval);
    }
    else if (key < CURLOPTTYPE_OFF_T) {
      curl_easy_setopt(curl_, key, opt->second.c_str());
    }
    else {
      //XXX throw
    }
  }
  isOpen_ = true;
}

void TCurlClient::cleanup() {
  if (headers_) {
    curl_slist_free_all(headers_);
    headers_ = NULL;
  }
  if (curl_) {
    curl_easy_cleanup(curl_);
    curl_ = NULL;
  }
  isOpen_ = false;
}

uint32_t TCurlClient::read(uint8_t* buf, uint32_t len) {
  uint32_t nread = readBuffer_.read(buf, len);
  return nread;
}

void TCurlClient::readEnd() {
  readBuffer_.resetBuffer();
}

void TCurlClient::write(const uint8_t* buf, uint32_t len) {
  writeBuffer_.write(buf, len);
}

void TCurlClient::flush() {
  CURLcode res;
  char error[CURL_ERROR_SIZE];
  long rc;

#ifndef USE_KEEPALIVE
  init();
#endif
#ifndef USE_CHUNKED
  curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE,
		   writeBuffer_.available_read());
#endif
  error[0] = '\0';
  curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, error);
  res = curl_easy_perform(curl_);
#ifndef USE_KEEPALIVE
  cleanup();
#endif
  
  if (res) {
    string msg =
      string("flush to ") + url_ + string(" failed: ") + error;
    throw TTransportException(msg);
  }

  curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &rc);
  if (rc != 200) {
    char code[4];
    string msg;

    snprintf(code, sizeof(code), "%d", rc);
    msg = string("flush to ") + url_ + string(" failed: ") + code;
    throw TTransportException(msg);
  }
}

}}} // apache::thrift::transport
