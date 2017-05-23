/**
 * @file httpnotifier_test.cpp UT for Memento HTTP notifier
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include <string>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "utils.h"
#include "sas.h"
#include "fakehttpresolver.hpp"
#include "httpconnection.h"
#include "httpnotifier.h"
#include "fakecurl.hpp"
#include "test_utils.hpp"

using namespace std;

/// Fixture for HttpNotifierTest.
class HttpNotifierTest : public ::testing::Test
{
  FakeHttpResolver _resolver;
  HttpNotifier _http_notifier;

  HttpNotifierTest() :
    _resolver("10.42.42.42"),
    _http_notifier(&_resolver, "http://notification.domain/notify")
  {
    fakecurl_responses.clear();
    fakecurl_responses["http://10.42.42.42:80/notify"] = "";
  }

  virtual ~HttpNotifierTest()
  {
    fakecurl_responses.clear();
    fakecurl_requests.clear();
  }
};


// Now test the higher-level methods.

TEST_F(HttpNotifierTest, Notify)
{
  bool ret = _http_notifier.send_notify("user@domain", 0);
  EXPECT_TRUE(ret);
  Request& req = fakecurl_requests["http://notification.domain:80/notify"];
  EXPECT_EQ("POST", req._method);
  EXPECT_EQ("{\"impu\":\"user@domain\"}", req._body);
}

/// Fixture for HttpNotifierEmptyTest.
class HttpNotifierEmptyTest : public ::testing::Test
{
  FakeHttpResolver _resolver;
  HttpNotifier _http_notifier;

  HttpNotifierEmptyTest() :
    _resolver("10.42.42.42"),
    _http_notifier(&_resolver, "")
  {
    fakecurl_responses.clear();
    fakecurl_responses["http://10.42.42.42:80/notify"] = "";
    fakecurl_requests.clear();
  }

  virtual ~HttpNotifierEmptyTest()
  {
    fakecurl_responses.clear();
    fakecurl_requests.clear();
  }
};


// Now test the higher-level methods.

TEST_F(HttpNotifierEmptyTest, Notify)
{
  bool ret = _http_notifier.send_notify("user@domain", 0);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(fakecurl_requests.empty());
}
