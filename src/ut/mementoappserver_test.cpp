/**
 * @file mementoappserver_test.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2014  Metaswitch Networks Ltd
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
#include "gtest/gtest.h"

#include "sip_common.hpp"
#include "mockappserver.hpp"
#include "mementoappserver.h"
#include "mockloadmonitor.hpp"
#include "mock_call_list_store.h"
#include "mock_call_list_store_processor.h"
#include "test_interposer.hpp"
#include "mock_cassandra_store.h"
#include "zmq_lvc.h"

using namespace std;
using testing::InSequence;
using testing::Return;
using ::testing::_;
using ::testing::StrictMock;

const static std::string known_stats[] = {
  "memento_completed_calls",
  "memento_failed_calls",
  "memento_not_recorded_overload",
  "memento_cassandra_read_latency",
  "memento_cassandra_write_latency",
};
const static std::string zmq_port = "6666";
const int num_known_stats = sizeof(known_stats) / sizeof(std::string);

/// Fixture for MementoAppServerTest.
class MementoAppServerTest : public SipCommonTest
{
public:
  static void SetUpTestCase()
  {
    SipCommonTest::SetUpTestCase();
    _helper = new MockAppServerTsxHelper();
    _clsp = new MockCallListStoreProcessor();
    _stats_aggregator = new LastValueCache(num_known_stats,
                                           known_stats,
                                           zmq_port,
                                           10);

    // Completely control time so we can match against
    // the start/answer/end times in the XML.
    cwtest_completely_control_time();
  }

  static void TearDownTestCase()
  {
    cwtest_reset_time();

    delete _stats_aggregator; _stats_aggregator = NULL;
    delete _clsp; _clsp = NULL;
    delete _helper; _helper = NULL;
    SipCommonTest::TearDownTestCase();
  }

  MementoAppServerTest() : SipCommonTest()
  {
  }

  ~MementoAppServerTest()
  {
  }

  static MockAppServerTsxHelper* _helper;
  static MockCallListStoreProcessor* _clsp;
  static LastValueCache* _stats_aggregator;
};

/// Fixture for MementoAppServerTest.
class MementoAppServerWithDialogIDTest : public SipCommonTest
{
public:
  static void SetUpTestCase()
  {
    SipCommonTest::SetUpTestCase();
    _helper = new MockAppServerTsxHelper("123_123_c2lwOjY1MDU1NTEyMzRAaG9tZWRvbWFpbg==");
    _clsp = new MockCallListStoreProcessor();
    _stats_aggregator = new LastValueCache(num_known_stats,
                                           known_stats,
                                           zmq_port,
                                           10);

    cwtest_completely_control_time();
  }

  static void TearDownTestCase()
  {
    cwtest_reset_time();

    delete _stats_aggregator; _stats_aggregator = NULL;
    delete _clsp; _clsp = NULL;
    delete _helper; _helper = NULL;
    SipCommonTest::TearDownTestCase();
  }

  MementoAppServerWithDialogIDTest() : SipCommonTest()
  {
  }

  ~MementoAppServerWithDialogIDTest()
  {
  }

  static MockAppServerTsxHelper* _helper;
  static MockCallListStoreProcessor* _clsp;
  static LastValueCache* _stats_aggregator;
};

MockAppServerTsxHelper* MementoAppServerTest::_helper = NULL;
MockCallListStoreProcessor* MementoAppServerTest::_clsp = NULL;
LastValueCache* MementoAppServerTest::_stats_aggregator = NULL;
MockAppServerTsxHelper* MementoAppServerWithDialogIDTest::_helper = NULL;
MockCallListStoreProcessor* MementoAppServerWithDialogIDTest::_clsp = NULL;
LastValueCache* MementoAppServerWithDialogIDTest::_stats_aggregator = NULL;

namespace MementoAS
{
class Message
{
public:
  string _method;
  string _toscheme;
  string _status;
  string _from;
  string _fromdomain;
  string _to;
  string _todomain;
  string _route;
  string _extra;

  Message() :
    _method("INVITE"),
    _toscheme("sip"),
    _status("200 OK"),
    _from("6505551000"),
    _fromdomain("homedomain"),
    _to("6505551234"),
    _todomain("homedomain"),
    _route(""),
    _extra("")
  {
  }

  string get_request();
  string get_response();
};
}

string MementoAS::Message::get_request()
{
  char buf[16384];

  // The remote target.
  string target = string(_toscheme).append(":").append(_to);
  if (!_todomain.empty())
  {
    target.append("@").append(_todomain);
  }

  int n = snprintf(buf, sizeof(buf),
                   "%1$s %4$s SIP/2.0\r\n"
                   "Via: SIP/2.0/TCP 10.114.61.213;branch=z9hG4bK0123456789abcdef\r\n"
                   "From: Alice <sip:%2$s@%3$s>;tag=10.114.61.213+1+8c8b232a+5fb751cf\r\n"
                   "To: Bob <%4$s>\r\n"
                   "%5$s"
                   "%6$s"
                   "Max-Forwards: 68\r\n"
                   "Call-ID: 0gQAAC8WAAACBAAALxYAAAL8P3UbW8l4mT8YBkKGRKc5SOHaJ1gMRqsUOO4ohntC@10.114.61.213\r\n"
                   "CSeq: 16567 %1$s\r\n"
                   "User-Agent: Accession 2.0.0.0\r\n"
                   "Allow: PRACK, INVITE, ACK, BYE, CANCEL, UPDATE, SUBSCRIBE, NOTIFY, REFER, MESSAGE, OPTIONS\r\n"
                   "Content-Length: 0\r\n\r\n",
                   /*  1 */ _method.c_str(),
                   /*  2 */ _from.c_str(),
                   /*  3 */ _fromdomain.c_str(),
                   /*  4 */ target.c_str(),
                   /*  5 */ _route.empty() ? "" : string(_route).append("\r\n").c_str(),
                   /*  6 */ _extra.empty() ? "" : string(_extra).append("\r\n").c_str()
    );

  EXPECT_LT(n, (int)sizeof(buf));

  string ret(buf, n);
  return ret;
}

string MementoAS::Message::get_response()
{
  char buf[16384];

  // The remote target.
  string target = string(_toscheme).append(":").append(_to);
  if (!_todomain.empty())
  {
    target.append("@").append(_todomain);
  }

  int n = snprintf(buf, sizeof(buf),
                   "SIP/2.0 %1$s\r\n"
                   "Via: SIP/2.0/TCP 10.114.61.213;branch=z9hG4bK0123456789abcdef\r\n"
                   "From: <sip:%2$s@%3$s>;tag=10.114.61.213+1+8c8b232a+5fb751cf\r\n"
                   "To: <sip:%4$s@%5$s>\r\n"
                   "%6$s"
                   "Max-Forwards: 68\r\n"
                   "Call-ID: 0gQAAC8WAAACBAAALxYAAAL8P3UbW8l4mT8YBkKGRKc5SOHaJ1gMRqsUOO4ohntC@10.114.61.213\r\n"
                   "CSeq: 16567 %7$s\r\n"
                   "User-Agent: Accession 2.0.0.0\r\n"
                   "Allow: PRACK, INVITE, ACK, BYE, CANCEL, UPDATE, SUBSCRIBE, NOTIFY, REFER, MESSAGE, OPTIONS\r\n"
                   "Content-Length: 0\r\n\r\n",
                   /*  1 */ _status.c_str(),
                   /*  2 */ _from.c_str(),
                   /*  3 */ _fromdomain.c_str(),
                   /*  4 */ _to.c_str(),
                   /*  5 */ _todomain.c_str(),
                   /*  6 */ _route.empty() ? "" : string(_route).append("\r\n").c_str(),
                   /*  7 */ _method.c_str()
    );

  EXPECT_LT(n, (int)sizeof(buf));

  string ret(buf, n);
  return ret;
}

using MementoAS::Message;

std::string get_formatted_timestamp();
void add_header(pjsip_routing_hdr* header, pj_str_t header_name, char* uri, pj_pool_t* pool);
void add_parameter(pjsip_routing_hdr* header,
                   pj_str_t param_name,
                   pj_str_t param_value,
                   pj_pool_t* pool);

// Test creation and destruction of the MementoAppServer objects
TEST_F(MementoAppServerTest, CreateMementoAppServer)
{
  // Create a MementoAppServer object
  std::string home_domain = "home.domain";
  MementoAppServer* mas = new MementoAppServer("memento",
                                               NULL, // Call list store
                                               home_domain,
                                               0,
                                               25,
                                               604800,
                                               _stats_aggregator,
                                               1000000,
                                               20,
                                               100.0,
                                               10.0,
                                               NULL, // Exception Handler
                                               NULL, // HTTP Resolver
                                               "http://example.com/notify");

  // Test creating an app server transaction with an invalid method -
  // it shouldn't be created.
  Message msg;
  msg._method = "OPTIONS";
  pjsip_msg* req = parse_msg(msg.get_request());
  MementoAppServerTsx* mast = (MementoAppServerTsx*)mas->get_app_tsx(_helper, req);
  EXPECT_TRUE(mast == NULL);

  // Try with a valid method (Invite). This creates the application server
  // transaction
  msg._method = "INVITE";
  req = parse_msg(msg.get_request());
  mast = (MementoAppServerTsx*)mas->get_app_tsx(_helper, req);
  EXPECT_TRUE(mast != NULL);
  delete mast; mast = NULL;

  // Try with a valid method (Bye). This creates the application server
  // transaction
  msg._method = "BYE";
  req = parse_msg(msg.get_request());
  mast = (MementoAppServerTsx*)mas->get_app_tsx(_helper, req);
  EXPECT_TRUE(mast != NULL);
  delete mast; mast = NULL;

  delete mas; mas = NULL;
}

// Test the mainline case for an incoming call
TEST_F(MementoAppServerTest, MainlineIncomingTest)
{
  Message msg;
  std::string service_name = "memento";
  std::string home_domain = "home.domain";
  MementoAppServerTsx as_tsx(_helper, _clsp, service_name, home_domain);

  // Message is parsed successfully.
  EXPECT_CALL(*_helper, add_to_dialog(_));
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx.on_initial_request(parse_msg(msg.get_request()));

  // On a 200 OK response the as_tsx generates a BEGIN call fragment
  // writes it to the call list store
  std::string timestamp = get_formatted_timestamp();

  // Check that the xml, impu and CallFragment type are as expected
  std::string xml = std::string("<to>\n\t<URI>sip:6505551234@homedomain</URI>\n</to>\n<from>" \
                                "\n\t<URI>sip:6505551000@homedomain</URI>\n\t<name>Alice</name>" \
                                "\n</from>\n<outgoing>0</outgoing>\n<start-time>").
                    append(timestamp).append("</start-time>\n<answered>1</answered>\n<answer-time>").
                    append(timestamp).append("</answer-time>\n\n");
  std::string impu = "sip:6505551234@homedomain";
  EXPECT_CALL(*_clsp, write_call_list_entry(impu, _, _, CallListStore::CallFragment::Type::BEGIN, xml, _));
  EXPECT_CALL(*_helper, send_response(_));
  pjsip_msg* rsp = parse_msg(msg.get_response());
  as_tsx.on_response(rsp, 0);
}

// Test the mainline case for an outgoing call
TEST_F(MementoAppServerTest, MainlineOutgoingTest)
{
  Message msg;
  std::string service_name = "memento";
  std::string home_domain = "home.domain";
  MementoAppServerTsx as_tsx(_helper, _clsp, service_name, home_domain);

  pjsip_msg* req = parse_msg(msg.get_request());

  // Add a P-Asserted _Identity header.
  void* mem = pj_pool_alloc(_pool, sizeof(pjsip_routing_hdr));
  pjsip_routing_hdr* p_asserted_id = (pjsip_routing_hdr*)mem;
  add_header(p_asserted_id, pj_str("P-Asserted-Identity"), "Alice <sip:6505550000@homedomain>", _pool);
  pjsip_msg_add_hdr(req, (pjsip_hdr*)p_asserted_id);

  // Add a P-Served-User header.
  mem = pj_pool_alloc(_pool, sizeof(pjsip_routing_hdr));
  pjsip_routing_hdr* p_served_user = (pjsip_routing_hdr*)mem;
  add_header(p_served_user, pj_str("P-Served-User"), "<sip:6505551234@homedomain>", _pool);
  add_parameter(p_served_user, pj_str("sescase"), pj_str("orig"), _pool);
  pjsip_msg_add_hdr(req, (pjsip_hdr*)p_served_user);

  EXPECT_CALL(*_helper, add_to_dialog(_));
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx.on_initial_request(req);

  // On a 200 OK response the as_tsx generates a BEGIN call fragment
  // writes it to the call list store
  std::string timestamp = get_formatted_timestamp();

  std::string xml = std::string("<to>\n\t<URI>sip:6505551234@homedomain</URI>\n\t<name>Bob</name>\n" \
                                "</to>\n<from>\n\t<URI>sip:6505550000@homedomain</URI>\n\t<name>Alice</name>\n" \
                                "</from>\n<outgoing>1</outgoing>\n<start-time>").
                    append(timestamp).append("</start-time>\n<answered>1</answered>\n<answer-time>").
                    append(timestamp).append("</answer-time>\n\n");
  std::string impu = "sip:6505550000@homedomain";
  EXPECT_CALL(*_clsp, write_call_list_entry(impu, _, _, CallListStore::CallFragment::Type::BEGIN, xml, _));
  EXPECT_CALL(*_helper, send_response(_));
  pjsip_msg* rsp = parse_msg(msg.get_response());
  as_tsx.on_response(rsp, 0);
}

// Test that, when the P Asserted ID is present on the (200) response, the answerer is logged
TEST_F(MementoAppServerTest, OnResponseRecordAnswerer)
{
  Message msg;
  std::string service_name = "memento";
  std::string home_domain = "home.domain";
  MementoAppServerTsx as_tsx(_helper, _clsp, service_name, home_domain);

  pjsip_msg* req = parse_msg(msg.get_request());

  // Add a P-Asserted-Identity header.
  void* mem = pj_pool_alloc(_pool, sizeof(pjsip_routing_hdr));
  pjsip_routing_hdr* p_asserted_id = (pjsip_routing_hdr*)mem;
  add_header(p_asserted_id, pj_str("P-Asserted-Identity"), "Alice <sip:6505550000@homedomain>", _pool);
  pjsip_msg_add_hdr(req, (pjsip_hdr*)p_asserted_id);

  // Add a P-Served-User header.
  mem = pj_pool_alloc(_pool, sizeof(pjsip_routing_hdr));
  pjsip_routing_hdr* p_served_user = (pjsip_routing_hdr*)mem;
  add_header(p_served_user, pj_str("P-Served-User"), "<sip:6505551234@homedomain>", _pool);
  add_parameter(p_served_user, pj_str("sescase"), pj_str("orig"), _pool);
  pjsip_msg_add_hdr(req, (pjsip_hdr*)p_served_user);

  EXPECT_CALL(*_helper, add_to_dialog(_));
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx.on_initial_request(req);

  // Build the response to send. This contains the answerer's P-A-I
  // (not necessarily the called URI, as Bob may have call forwarding)
  pjsip_msg* rsp = parse_msg(msg.get_response());
  mem = pj_pool_alloc(_pool, sizeof(pjsip_routing_hdr));
  p_asserted_id = (pjsip_routing_hdr*)mem;
  add_header(p_asserted_id, pj_str("P-Asserted-Identity"), "Bob's cell <sip:6505551235@homedomain>", _pool);
  pjsip_msg_add_hdr(rsp, (pjsip_hdr*)p_asserted_id);

  // On a 200 OK response the as_tsx generates a BEGIN call fragment
  // writes it to the call list store. This contains the answerer
  std::string timestamp = get_formatted_timestamp();

  std::string xml = std::string("<to>\n\t<URI>sip:6505551234@homedomain</URI>\n\t<name>Bob</name>\n" \
                                "</to>\n<from>\n\t<URI>sip:6505550000@homedomain</URI>\n\t<name>Alice</name>\n" \
                                "</from>\n<outgoing>1</outgoing>\n<start-time>").
                    append(timestamp).append("</start-time>\n<answered>1</answered>\n<answer-time>").
                    append(timestamp).append("</answer-time>\n<answerer>\n\t<URI>sip:6505551235@homedomain</URI>\n" \
                                "\t<name>Bob&apos;s cell</name>\n</answerer>\n\n");
  std::string impu = "sip:6505550000@homedomain";
  EXPECT_CALL(*_clsp, write_call_list_entry(impu, _, _, CallListStore::CallFragment::Type::BEGIN, xml, _));
  EXPECT_CALL(*_helper, send_response(_));
  as_tsx.on_response(rsp, 0);
}

// Test that, when privacy is requested, the answerer is not logged
TEST_F(MementoAppServerTest, OnResponseNotRecordPrivateAnswerer)
{
  Message msg;
  std::string service_name = "memento";
  std::string home_domain = "home.domain";
  MementoAppServerTsx as_tsx(_helper, _clsp, service_name, home_domain);

  pjsip_msg* req = parse_msg(msg.get_request());

  // Add a P-Asserted-Identity header.
  void* mem = pj_pool_alloc(_pool, sizeof(pjsip_routing_hdr));
  pjsip_routing_hdr* p_asserted_id = (pjsip_routing_hdr*)mem;
  add_header(p_asserted_id, pj_str("P-Asserted-Identity"), "Alice <sip:6505550000@homedomain>", _pool);
  pjsip_msg_add_hdr(req, (pjsip_hdr*)p_asserted_id);

  // Add a P-Served-User header.
  mem = pj_pool_alloc(_pool, sizeof(pjsip_routing_hdr));
  pjsip_routing_hdr* p_served_user = (pjsip_routing_hdr*)mem;
  add_header(p_served_user, pj_str("P-Served-User"), "<sip:6505551234@homedomain>", _pool);
  add_parameter(p_served_user, pj_str("sescase"), pj_str("orig"), _pool);
  pjsip_msg_add_hdr(req, (pjsip_hdr*)p_served_user);

  EXPECT_CALL(*_helper, add_to_dialog(_));
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx.on_initial_request(req);

  // Build the response to send. This contains the answerer's P-A-I
  // (not necessarily the called URI, as Bob may have call forwarding)
  pjsip_msg* rsp = parse_msg(msg.get_response());
  mem = pj_pool_alloc(_pool, sizeof(pjsip_routing_hdr));
  p_asserted_id = (pjsip_routing_hdr*)mem;
  add_header(p_asserted_id, pj_str("P-Asserted-Identity"), "Bob's cell <sip:6505551235@homedomain>", _pool);
  pjsip_msg_add_hdr(rsp, (pjsip_hdr*)p_asserted_id);

  // Add a Privacy header with value 'id'.
  pj_str_t* privacy_name = pj_strset((pj_str_t*)pj_pool_alloc(_pool, sizeof(pj_str_t)), "Privacy", 7);
  pj_str_t* privacy_value = pj_strset((pj_str_t*)pj_pool_alloc(_pool, sizeof(pj_str_t)), "id", 2);
  pjsip_generic_string_hdr* privacy = pjsip_generic_string_hdr_create(_pool, privacy_name, privacy_value);
  pjsip_msg_add_hdr(rsp, (pjsip_hdr*)privacy);

  // On a 200 OK response the as_tsx generates a BEGIN call fragment
  // writes it to the call list store. This doesn't contain the answerer
  std::string timestamp = get_formatted_timestamp();

  std::string xml = std::string("<to>\n\t<URI>sip:6505551234@homedomain</URI>\n\t<name>Bob</name>\n" \
                                "</to>\n<from>\n\t<URI>sip:6505550000@homedomain</URI>\n\t<name>Alice</name>\n" \
                                "</from>\n<outgoing>1</outgoing>\n<start-time>").
                    append(timestamp).append("</start-time>\n<answered>1</answered>\n<answer-time>").
                    append(timestamp).append("</answer-time>\n\n");
  std::string impu = "sip:6505550000@homedomain";
  EXPECT_CALL(*_clsp, write_call_list_entry(impu, _, _, CallListStore::CallFragment::Type::BEGIN, xml, _));
  EXPECT_CALL(*_helper, send_response(_));
  as_tsx.on_response(rsp, 0);
}

// Test when the P Asserted ID is missing
TEST_F(MementoAppServerTest, OutgoingMissingPAssertedHeaderTest)
{
  Message msg;
  std::string service_name = "memento";
  std::string home_domain = "home.domain";
  MementoAppServerTsx as_tsx(_helper, _clsp, service_name, home_domain);

  // Add a P-Served-User header.
  pjsip_msg* req = parse_msg(msg.get_request());
  void* mem = pj_pool_alloc(_pool, sizeof(pjsip_routing_hdr));
  pjsip_routing_hdr* p_served_user = (pjsip_routing_hdr*)mem;
  add_header(p_served_user, pj_str("P-Served-User"), "<sip:6505551234@homedomain>", _pool);
  add_parameter(p_served_user, pj_str("sescase"), pj_str("orig"), _pool);
  pjsip_msg_add_hdr(req, (pjsip_hdr*)p_served_user);

  // Message is parsed and rejected.
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx.on_initial_request(req);
}

// Test that a non final response doesn't trigger writes to cassandra
TEST_F(MementoAppServerTest, OnNonFinalResponse)
{
  Message msg;
  std::string service_name = "memento";
  std::string home_domain = "home.domain";
  MementoAppServerTsx as_tsx(_helper, _clsp, service_name, home_domain);

  EXPECT_CALL(*_helper, add_to_dialog(_));
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx.on_initial_request(parse_msg(msg.get_request()));

  msg._status = "100 Trying";
  pjsip_msg* rsp = parse_msg(msg.get_response());
  EXPECT_CALL(*_helper, send_response(_));
  as_tsx.on_response(rsp, 0);
}

// Test that multiple responses don't provoke multiple writes to cassandra
TEST_F(MementoAppServerTest, OnMultipleResponses)
{
  Message msg;
  std::string service_name = "memento";
  std::string home_domain = "home.domain";
  MementoAppServerTsx as_tsx(_helper, _clsp, service_name, home_domain);

  EXPECT_CALL(*_helper, add_to_dialog(_));
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx.on_initial_request(parse_msg(msg.get_request()));

  pjsip_msg* rsp = parse_msg(msg.get_response());
  EXPECT_CALL(*_clsp, write_call_list_entry(_, _, _, _, _, _));
  EXPECT_CALL(*_helper, send_response(_));
  as_tsx.on_response(rsp, 0);

  // Send in another response - this should return straightaway
  EXPECT_CALL(*_helper, send_response(_));
  as_tsx.on_response(rsp, 0);
}

// Test that a non final response doesn't trigger writes to cassandra
TEST_F(MementoAppServerTest, OnNonInviteResponse)
{
  Message msg;
  msg._method = "BYE";
  std::string service_name = "memento";
  std::string home_domain = "home.domain";
  MementoAppServerTsx as_tsx(_helper, _clsp, service_name, home_domain);

  EXPECT_CALL(*_helper, add_to_dialog(_));
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx.on_initial_request(parse_msg(msg.get_request()));

  pjsip_msg* rsp = parse_msg(msg.get_response());
  EXPECT_CALL(*_helper, send_response(_));
  as_tsx.on_response(rsp, 0);
}

// Test that an error response triggers a REJECTED call fragment.
TEST_F(MementoAppServerTest, OnErrorResponse)
{
  Message msg;
  std::string service_name = "memento";
  std::string home_domain = "home.domain";
  MementoAppServerTsx as_tsx(_helper, _clsp, service_name, home_domain);

  EXPECT_CALL(*_helper, add_to_dialog(_));
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx.on_initial_request(parse_msg(msg.get_request()));

  msg._status = "404 Not Found";
  std::string timestamp = get_formatted_timestamp();

  // Check that the xml, impu and CallFragment type are as expected
  std::string xml = std::string("<to>\n\t<URI>sip:6505551234@homedomain</URI>\n</to>\n<from>" \
                                "\n\t<URI>sip:6505551000@homedomain</URI>\n\t<name>Alice</name>" \
                                "\n</from>\n<outgoing>0</outgoing>\n<start-time>").
                    append(timestamp).append("</start-time>\n<answered>0</answered>\n\n");
  std::string impu = "sip:6505551234@homedomain";
  EXPECT_CALL(*_clsp, write_call_list_entry(impu, _, _, CallListStore::CallFragment::Type::REJECTED, xml, _));
  EXPECT_CALL(*_helper, send_response(_));
  pjsip_msg* rsp = parse_msg(msg.get_response());
  as_tsx.on_response(rsp, 0);
}

// Test an on in dialog response for an incoming call.
TEST_F(MementoAppServerWithDialogIDTest, OnInDialogRequestTest)
{
  Message msg;
  std::string service_name = "memento";
  std::string home_domain = "home.domain";

  // Intial INVITE transaction
  MementoAppServerTsx as_tsx_initial(_helper, _clsp, service_name, home_domain);

  // Message is parsed successfully. The on_initial_request method
  // adds a Record-Route header.
  EXPECT_CALL(*_helper, add_to_dialog(_));
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx_initial.on_initial_request(parse_msg(msg.get_request()));

  // On a 200 OK response the as_tsx_initial generates a BEGIN call fragment
  // writes it to the call list store
  EXPECT_CALL(*_clsp, write_call_list_entry(_, _, _, _, _, _));
  EXPECT_CALL(*_helper, send_response(_));
  pjsip_msg* rsp = parse_msg(msg.get_response());
  as_tsx_initial.on_response(rsp, 0);

  // In dialog reINVITE transaction
  MementoAppServerTsx as_tsx_during(_helper, _clsp, service_name, home_domain);

  // On a reINVITE in dialog request, nothing is written to the store
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  as_tsx_during.on_in_dialog_request(parse_msg(msg.get_request()));

  // On a 200 OK response to that request, nothing is written to the store
  EXPECT_CALL(*_helper, send_response(_));
  rsp = parse_msg(msg.get_response());
  as_tsx_during.on_response(rsp, 0);

  // In dialog BYE transaction
  MementoAppServerTsx as_tsx_end(_helper, _clsp, service_name, home_domain);
  msg._method = "BYE";

  // On a BYE in dialog request the as_tsx_initial generates an END call
  // fragment and writes it to the call list store.
  std::string impu = "sip:6505551234@homedomain";
  std::string timestamp = get_formatted_timestamp();

  std::string xml = std::string("<end-time>").append(timestamp).append("</end-time>\n\n");
  EXPECT_CALL(*_helper, send_request(_)).WillOnce(Return(0));
  EXPECT_CALL(*_clsp, write_call_list_entry(impu, _, _, CallListStore::CallFragment::Type::END, xml, _));
  as_tsx_end.on_in_dialog_request(parse_msg(msg.get_request()));

  // On a 200 OK response to that BYE, nothing is written to the store
  EXPECT_CALL(*_helper, send_response(_));
  rsp = parse_msg(msg.get_response());
  as_tsx_end.on_response(rsp, 0);
}

std::string get_formatted_timestamp()
{
  time_t currenttime;
  time(&currenttime);
  tm* ct = localtime(&currenttime);
  return create_formatted_timestamp(ct, "%Y-%m-%dT%H:%M:%S");
}

void add_header(pjsip_routing_hdr* header, pj_str_t header_name, char* uri, pj_pool_t* pool)
{
  pj_list_init(header);
  header->type = PJSIP_H_OTHER;
  header->name = header_name;
  header->sname = pj_str("");
  pjsip_name_addr_init(&header->name_addr);
  pj_list_init(&header->other_param);

  pjsip_name_addr* temp = (pjsip_name_addr*)uri_from_string(uri, pool, true);
  memcpy(&header->name_addr, temp, sizeof(pjsip_name_addr));
}

void add_parameter(pjsip_routing_hdr* header,
                   pj_str_t param_name,
                   pj_str_t param_value,
                   pj_pool_t* pool)
{
  pjsip_param* param = PJ_POOL_ALLOC_T(pool, pjsip_param);
  param->name = param_name;
  param->value = param_value;
  pj_list_insert_before(&header->other_param, param);
}
