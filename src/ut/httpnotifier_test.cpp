/**
 * @file httpnotifier_test.cpp UT for Memento HTTP notifier
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2015  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
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
  Request& req = fakecurl_requests["http://10.42.42.42:80/notify"];
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
