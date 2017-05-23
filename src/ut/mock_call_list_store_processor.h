/**
 * @file mock_call_list_store_processor.h Mock call list store object.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCK_CALL_LIST_STORE_PROCESSOR_H_
#define MOCK_CALL_LIST_STORE_PROCESSOR_H_

#include "call_list_store_processor.h"

class MockCallListStoreProcessor : public CallListStoreProcessor
{
public:
  MockCallListStoreProcessor() : CallListStoreProcessor(NULL, NULL, 0, 0, 0, NULL, NULL, NULL) {}
  virtual ~MockCallListStoreProcessor() {};

  MOCK_METHOD6(write_call_list_entry, void(std::string impu,
                                           std::string timestamp,
                                           std::string id,
                                           CallListStore::CallFragment::Type type,
                                           std::string xml,
                                           SAS::TrailId trail));
};

#endif
