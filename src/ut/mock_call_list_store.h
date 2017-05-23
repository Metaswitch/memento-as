/**
 * @file mock_call_list_store.h Mock call list store object.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCK_CALL_LIST_STORE_H_
#define MOCK_CALL_LIST_STORE_H_

#include "call_list_store.h"
#include "mock_cassandra_store.h"

class MockCallListStore : public CallListStore::Store
{
public:
  virtual ~MockCallListStore() {};

  MOCK_METHOD4(new_write_call_fragment_op,
               CallListStore::WriteCallFragment*(const std::string& impu,
                                                 const CallListStore::CallFragment& fragment,
                                                 const int64_t cass_timestamp,
                                                 const int32_t ttl));

  MOCK_METHOD1(new_get_call_fragments_op,
               CallListStore::GetCallFragments*(const std::string& impu));

  MOCK_METHOD3(new_delete_old_call_fragments_op,
               CallListStore::DeleteOldCallFragments*(const std::string& impu,
                                                      const std::vector<CallListStore::CallFragment> fragments,
                                                      const int64_t cass_timestamp));

  MOCK_METHOD5(write_call_fragment_sync,
               CassandraStore::ResultCode(const std::string& impu,
                                          const CallListStore::CallFragment& fragment,
                                          const int64_t cass_timestamp,
                                          const int32_t ttl,
                                          SAS::TrailId trail));
  MOCK_METHOD3(get_call_fragments_sync,
               CassandraStore::ResultCode(const std::string& impu,
                                          std::vector<CallListStore::CallFragment>& fragments,
                                          SAS::TrailId trail));

  MOCK_METHOD4(delete_old_call_fragments_sync,
               CassandraStore::ResultCode(const std::string& impu,
                                          const std::vector<CallListStore::CallFragment> fragments,
                                          const int64_t cass_timestamp,
                                          SAS::TrailId trail));
};

#endif

