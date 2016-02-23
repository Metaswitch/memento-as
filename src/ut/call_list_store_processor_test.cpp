/**
 * @file call_list_store_processor.cpp
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
#include <time.h>

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test_utils.hpp"
#include "test_interposer.hpp"
#include "fakelogger.h"

#include "call_list_store_processor.h"
#include "mock_call_list_store.h"
#include "mockloadmonitor.hpp"
#include "mockhttpnotifier.h"
#include "memento_lvc.h"

using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::_;
using ::testing::StrictMock;
using ::testing::Mock;
using ::testing::DoAll;

static int CALL_LIST_TTL = 604800;
static int FAKE_SAS_TRAIL = 0;
static std::string IMPU = "sip:6510001000@home.domain";
static std::string TIMESTAMP = "20020530093010";

const static std::string known_stats[] = {
  "memento_completed_calls",
  "memento_failed_calls",
  "memento_not_recorded_overload",
  "memento_cassandra_read_latency",
  "memento_cassandra_write_latency",
};
const static std::string zmq_port = "6666";
const int num_known_stats = sizeof(known_stats) / sizeof(std::string);

// Fixture for tests that has no limit to the number of stored call lists
class CallListStoreProcessorTest : public ::testing::Test
{
public:
  CallListStoreProcessorTest()
  {
    _cls = new MockCallListStore();
    _stats_aggregator = new LastValueCache(num_known_stats,
                                           known_stats,
                                           zmq_port,
                                           10);
    _http_notifier = new MockHttpNotifier();

    // No maximum call length and 1 worker thread
    _clsp = new CallListStoreProcessor(&_load_monitor, _cls, 0, 1, CALL_LIST_TTL, _stats_aggregator, NULL, _http_notifier);
  }

  virtual ~CallListStoreProcessorTest()
  {
    delete _cls; _cls = NULL;
    delete _stats_aggregator; _stats_aggregator = NULL;
    delete _clsp; _clsp = NULL;
    delete _http_notifier; _http_notifier = NULL;
  }

  StrictMock<MockLoadMonitor> _load_monitor;
  CallListStoreProcessor* _clsp;
  MockCallListStore* _cls;
  LastValueCache* _stats_aggregator;
  MockHttpNotifier* _http_notifier;
};

// Fixture for tests that has a low limit to the number of stored call lists
class CallListStoreProcessorWithLimitTest : public ::testing::Test
{
public:
  CallListStoreProcessorWithLimitTest()
  {
    _cls = new MockCallListStore();
    _stats_aggregator = new LastValueCache(num_known_stats,
                                           known_stats,
                                           zmq_port,
                                           10);
    _http_notifier = new MockHttpNotifier();

    // Maximum call length of 4 and 2 worker threads
    _clsp = new CallListStoreProcessor(&_load_monitor, _cls, 4, 2, CALL_LIST_TTL, _stats_aggregator, NULL, _http_notifier);
  }

  virtual ~CallListStoreProcessorWithLimitTest()
  {
    delete _cls; _cls = NULL;
    delete _stats_aggregator; _stats_aggregator = NULL;
    delete _clsp; _clsp = NULL;
    delete _http_notifier; _http_notifier = NULL;
  }

  StrictMock<MockLoadMonitor> _load_monitor;
  CallListStoreProcessor* _clsp;
  MockCallListStore* _cls;
  LastValueCache* _stats_aggregator;
  MockHttpNotifier* _http_notifier;
};

// Create a vector of call list store fragments that the mock
// get_call_fragments_sync can return. It creates 7 records
// making up 6 calls; the first two match the begin and end of
// a call, then last five are all single rejected calls.
void create_records(std::vector<CallListStore::CallFragment>& records)
{
  CallListStore::CallFragment record1;
  CallListStore::CallFragment record2;
  CallListStore::CallFragment record3;
  CallListStore::CallFragment record4;
  CallListStore::CallFragment record5;
  CallListStore::CallFragment record6;
  CallListStore::CallFragment record7;

  record1.type = CallListStore::CallFragment::Type::BEGIN;
  record1.timestamp = "20020530093010";
  record1.id = "a";
  record1.contents =
    "<to>"
      "<URI>alice@example.com</URI>"
      "<name>Alice Adams</name>"
    "</to>"
    "<from>"
      "<URI>bob@example.com</URI>"
      "<name>Bob Barker</name>"
    "</from>"
    "<answered>1</answered>"
    "<outgoing>1</outgoing>"
    "<start-time>2002-05-30T09:30:10</start-time>"
    "<answer-time>2002-05-30T09:30:20</answer-time>";

  record2.type = CallListStore::CallFragment::Type::END;
  record2.timestamp = "20020530093010";
  record2.id = "a";
  record2.contents = "<end-time>2002-05-30T09:35:00</end-time>";

  record3.type = CallListStore::CallFragment::Type::REJECTED;
  record3.timestamp = "20020530093011";
  record3.id = "b";
  record3.contents =
    "<to>"
      "<URI>alice@example.net</URI>"
      "<name>Alice Adams</name>"
    "</to>"
    "<from>"
      "<URI>bob@example.net</URI>"
      "<name>Bob Barker</name>"
    "</from>"
    "<answered>0</answered>"
    "<outgoing>1</outgoing>"
    "<start-time>2002-05-30T09:30:11</start-time>";

  record4.type = CallListStore::CallFragment::Type::REJECTED;
  record4.timestamp = "20020530093012";
  record4.id = "c";
  record4.contents =
    "<to>"
      "<URI>alice@example.net</URI>"
      "<name>Alice Adams</name>"
    "</to>"
    "<from>"
      "<URI>bob@example.net</URI>"
      "<name>Bob Barker</name>"
    "</from>"
    "<answered>0</answered>"
    "<outgoing>1</outgoing>"
    "<start-time>2002-05-30T09:30:12</start-time>";

  record5.type = CallListStore::CallFragment::Type::REJECTED;
  record5.timestamp = "20020530093013";
  record5.id = "d";
  record5.contents =
    "<to>"
      "<URI>alice@example.net</URI>"
      "<name>Alice Adams</name>"
    "</to>"
    "<from>"
      "<URI>bob@example.net</URI>"
      "<name>Bob Barker</name>"
    "</from>"
    "<answered>0</answered>"
    "<outgoing>1</outgoing>"
    "<start-time>2002-05-30T09:30:13</start-time>";

  record6.type = CallListStore::CallFragment::Type::REJECTED;
  record6.timestamp = "20020530093014";
  record6.id = "e";
  record6.contents =
    "<to>"
      "<URI>alice@example.net</URI>"
      "<name>Alice Adams</name>"
    "</to>"
    "<from>"
      "<URI>bob@example.net</URI>"
      "<name>Bob Barker</name>"
    "</from>"
    "<answered>0</answered>"
    "<outgoing>1</outgoing>"
    "<start-time>2002-05-30T09:30:14</start-time>";

  record7.type = CallListStore::CallFragment::Type::REJECTED;
  record7.timestamp = "20020530093015";
  record7.id = "f";
  record7.contents =
    "<to>"
      "<URI>alice@example.net</URI>"
      "<name>Alice Adams</name>"
    "</to>"
    "<from>"
      "<URI>bob@example.net</URI>"
      "<name>Bob Barker</name>"
    "</from>"
    "<answered>0</answered>"
    "<outgoing>1</outgoing>"
    "<start-time>2002-05-30T09:30:15</start-time>";

  records.push_back(record1);
  records.push_back(record2);
  records.push_back(record3);
  records.push_back(record4);
  records.push_back(record5);
  records.push_back(record6);
  records.push_back(record7);
}

// Test that if the Call List store has no maximum length that the
// call record count needed function returns false.
TEST_F(CallListStoreProcessorTest, CallListIsCountNeededNoLimit)
{
  std::vector<CallListStore::CallFragment> fragments;
  bool rc =_clsp->_thread_pool->is_call_trim_needed(IMPU, fragments, FAKE_SAS_TRAIL);
  ASSERT_FALSE(rc);
  ASSERT_TRUE(fragments.size() == 0);
}

TEST_F(CallListStoreProcessorTest, CallListWriteBegin)
{
  EXPECT_CALL(_load_monitor, request_complete(_)).Times(1);
  EXPECT_CALL(*_http_notifier, send_notify(_, _)).Times(1);

  EXPECT_CALL(*_cls, write_call_fragment_sync(IMPU, _, _, CALL_LIST_TTL, FAKE_SAS_TRAIL)).WillOnce(Return(CassandraStore::ResultCode::OK));
  _clsp->write_call_list_entry(IMPU, TIMESTAMP, "id", CallListStore::CallFragment::Type::BEGIN, "xml", FAKE_SAS_TRAIL);
  sleep(1);
}

TEST_F(CallListStoreProcessorTest, CallListWriteEnd)
{
  EXPECT_CALL(_load_monitor, request_complete(_)).Times(1);
  EXPECT_CALL(*_http_notifier, send_notify(_, _)).Times(1);

  EXPECT_CALL(*_cls, write_call_fragment_sync(IMPU, _, _, CALL_LIST_TTL, FAKE_SAS_TRAIL)).WillOnce(Return(CassandraStore::ResultCode::OK));
  _clsp->write_call_list_entry(IMPU, TIMESTAMP, "id", CallListStore::CallFragment::Type::END, "xml", FAKE_SAS_TRAIL);
  sleep(1);
}

TEST_F(CallListStoreProcessorTest, CallListWriteRejected)
{
  EXPECT_CALL(_load_monitor, request_complete(_)).Times(1);
  EXPECT_CALL(*_http_notifier, send_notify(_, _)).Times(1);

  EXPECT_CALL(*_cls, write_call_fragment_sync(IMPU, _, _, CALL_LIST_TTL, FAKE_SAS_TRAIL)).WillOnce(Return(CassandraStore::ResultCode::OK));
  _clsp->write_call_list_entry(IMPU, TIMESTAMP, "id", CallListStore::CallFragment::Type::REJECTED, "xml", FAKE_SAS_TRAIL);
  sleep(1);
}

TEST_F(CallListStoreProcessorTest, CallListWriteWithError)
{
  EXPECT_CALL(_load_monitor, request_complete(_)).Times(1);
  EXPECT_CALL(*_cls, write_call_fragment_sync(_, _, _, CALL_LIST_TTL, FAKE_SAS_TRAIL)).WillOnce(Return(CassandraStore::ResultCode::CONNECTION_ERROR));
  _clsp->write_call_list_entry(IMPU, TIMESTAMP, "id", CallListStore::CallFragment::Type::BEGIN, "xml", FAKE_SAS_TRAIL);
  sleep(1);
}

TEST_F(CallListStoreProcessorWithLimitTest, CallListWriteWithTrim)
{
  std::vector<CallListStore::CallFragment> records;
  create_records(records);

  EXPECT_CALL(_load_monitor, request_complete(_)).Times(1);
  EXPECT_CALL(*_http_notifier, send_notify(_, _)).Times(1);
  EXPECT_CALL(*_cls, write_call_fragment_sync(_, _, _, _, _)).WillOnce(Return(CassandraStore::ResultCode::OK));
  EXPECT_CALL(*_cls, get_call_fragments_sync(_,_,_)).WillOnce(DoAll(SetArgReferee<1>(records),
                                                                    Return(CassandraStore::ResultCode::OK)));
  EXPECT_CALL(*_cls, delete_old_call_fragments_sync(_,_,_,_)).WillOnce(Return(CassandraStore::ResultCode::OK));
  _clsp->write_call_list_entry(IMPU, TIMESTAMP, "id", CallListStore::CallFragment::Type::BEGIN, "xml", FAKE_SAS_TRAIL);
  sleep(1);
}

// Test with a max call list length of 4, and where 7 records (6 calls) have
// been returned. The is_call_trim_needed function should return true, and
// set the timestamp to the 5th oldest call
TEST_F(CallListStoreProcessorWithLimitTest, CallListIsCallTrimNeeded)
{
  std::vector<CallListStore::CallFragment> records;
  create_records(records);

  EXPECT_CALL(*_cls, get_call_fragments_sync(_,_,_)).WillOnce(DoAll(SetArgReferee<1>(records),
                                                                    Return(CassandraStore::ResultCode::OK)));

  std::vector<CallListStore::CallFragment> fragments;
  bool rc =_clsp->_thread_pool->is_call_trim_needed(IMPU, fragments, FAKE_SAS_TRAIL);

  ASSERT_TRUE(rc);
  ASSERT_TRUE(fragments.size() == 2);
}

// Test where getting the call records from the call list store fails when
// testing if the call list needs trimming. This should return false.
TEST_F(CallListStoreProcessorWithLimitTest, CallListIsCallTrimNeededCassError)
{
  std::vector<CallListStore::CallFragment> records;

  EXPECT_CALL(*_cls, get_call_fragments_sync(_,_,_)).WillOnce(DoAll(SetArgReferee<1>(records),
                                                                    Return(CassandraStore::ResultCode::UNKNOWN_ERROR)));

  std::vector<CallListStore::CallFragment> fragments;
  bool rc =_clsp->_thread_pool->is_call_trim_needed(IMPU, fragments, FAKE_SAS_TRAIL);

  ASSERT_FALSE(rc);
  ASSERT_TRUE(fragments.size() == 0);
}

// Test performing a call trim where the number of returned records is
// greater than 110% of the max_call_list_length
TEST_F(CallListStoreProcessorWithLimitTest, CallListPerformCallTrim)
{
  EXPECT_CALL(*_cls, delete_old_call_fragments_sync(_,_,_,_)).WillOnce(Return(CassandraStore::ResultCode::OK));
  std::vector<CallListStore::CallFragment> fragments;
  _clsp->_thread_pool->perform_call_trim(IMPU, fragments, 123, FAKE_SAS_TRAIL);
  ASSERT_TRUE(fragments.size() == 0);
}

// Test where deleting the call records from the call list store fails when
// testing if the call list needs trimming.
TEST_F(CallListStoreProcessorWithLimitTest, CallListPerformCallTrimCassError)
{
  EXPECT_CALL(*_cls, delete_old_call_fragments_sync(_,_,_,_)).WillOnce(Return(CassandraStore::ResultCode::UNKNOWN_ERROR));
  std::vector<CallListStore::CallFragment> fragments;
  _clsp->_thread_pool->perform_call_trim(IMPU, fragments, 0, FAKE_SAS_TRAIL);
}
