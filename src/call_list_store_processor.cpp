/**
 * @file call_list_store_processor.cpp
 *
 * Project Clearwater - IMS in the cloud.
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
#include "call_list_store_processor.h"

/// Constructor.
CallListStoreProcessor::CallListStoreProcessor(LoadMonitor* load_monitor,
                                               CallListStore::Store* call_list_store,
                                               const int max_call_list_length,
                                               const int memento_threads,
                                               const int call_list_ttl,
                                               LastValueCache* stats_aggregator,
                                               ExceptionHandler* exception_handler,
                                               HttpNotifier* http_notifier) :
  _thread_pool(new Pool(this,
                        call_list_store,
                        load_monitor,
                        max_call_list_length,
                        call_list_ttl,
                        memento_threads,
                        exception_handler,
                        &exception_callback,
                        http_notifier)),
  _stat_completed_calls_recorded("memento_completed_calls", stats_aggregator),
  _stat_failed_calls_recorded("memento_failed_calls", stats_aggregator),
  _stat_cassandra_read_latency("memento_cassandra_read_latency", stats_aggregator),
  _stat_cassandra_write_latency("memento_cassandra_write_latency", stats_aggregator)
{
  _thread_pool->start();
}

/// Destructor.
CallListStoreProcessor::~CallListStoreProcessor()
{
  if (_thread_pool != NULL)
  {
    _thread_pool->stop();
    _thread_pool->join();
    delete _thread_pool; _thread_pool = NULL;
  }
}

/// Creates a call list entry and adds it to the queue.
void CallListStoreProcessor::write_call_list_entry(
                                      std::string impu,
                                      std::string timestamp,
                                      std::string id,
                                      CallListStore::CallFragment::Type type,
                                      std::string xml,
                                      SAS::TrailId trail)
{
  // Create stop watch to time how long between the CallListStoreProcessor
  // receives the request, and a worker thread finishes processing it.

  // Create a call list entry and populate it
  CallListRequest* clr = new CallListStoreProcessor::CallListRequest();

  clr->impu = impu;
  clr->timestamp = timestamp;
  clr->id = id;
  clr->type = type;
  clr->contents = xml;
  clr->trail = trail;
  clr->stop_watch.start();

  _thread_pool->add_work(clr);
}

// Write the call list entry to the call list store
void CallListStoreProcessor::Pool::process_work(
                                  CallListStoreProcessor::CallListRequest*& clr)
{
  // Create the CallFragment
  CallListStore::CallFragment call_fragment;
  call_fragment.type = clr->type;
  call_fragment.id = clr->id;
  call_fragment.contents = clr->contents;
  call_fragment.timestamp = clr->timestamp;

  // Create the cassandra timestamp
  uint64_t cass_timestamp = CallListStore::Store::generate_timestamp();

  unsigned long latency_us;
  Utils::StopWatch stop_watch;
  stop_watch.start();

  CassandraStore::ResultCode rc = _call_list_store->write_call_fragment_sync(
                                                    clr->impu,
                                                    call_fragment,
                                                    cass_timestamp,
                                                    _call_list_ttl,
                                                    clr->trail);

  if (rc == CassandraStore::OK)
  {
    // Record the latency.
    latency_us = 0;
    if (stop_watch.read(latency_us))
    {
      _call_list_store_proc->_stat_cassandra_write_latency.accumulate(latency_us);
    }

    // Record that we have successfully written a call record.
    if (clr->type == CallListStore::CallFragment::Type::END)
    {
      _call_list_store_proc->_stat_completed_calls_recorded.increment();
    }
    else if (clr->type == CallListStore::CallFragment::Type::REJECTED)
    {
      _call_list_store_proc->_stat_failed_calls_recorded.increment();
    }

    // Reduce the number of stored calls (if necessary)
    std::vector<CallListStore::CallFragment> records_to_delete;

    if (is_call_trim_needed(clr->impu, records_to_delete, clr->trail))
    {
      perform_call_trim(clr->impu, records_to_delete, cass_timestamp, clr->trail);
    }

    // Notify anyone listening for updates
    if (_http_notifier != NULL)
    {
      _http_notifier->send_notify(clr->impu, clr->trail);
    }
  }
  else
  {
    // The write failed - log this and don't retry
    TRC_ERROR("Writing call list entry for IMPU: %s failed with rc %d",
                                                clr->impu.c_str(), rc);
  }

  // Record the latency of the request
  latency_us = 0;
  if (clr->stop_watch.read(latency_us))
  {
    TRC_DEBUG("Request latency = %ldus", latency_us);
    _load_monitor->request_complete(latency_us);
  }

  delete clr; clr = NULL;
}

// If the number of stored calls is greater than 110% of the max_call_list_length
// then delete older calls to bring the stored number below the threshold again.
// Checking the number of stored calls is done on average every
// 1 (max_call_list_length / 10) calls.
void CallListStoreProcessor::Pool::perform_call_trim(
                    std::string impu,
                    std::vector<CallListStore::CallFragment>& records_to_delete,
                    uint64_t cass_timestamp,
                    SAS::TrailId trail)
{
  // Delete the old records
  CassandraStore::ResultCode rc =
          _call_list_store->delete_old_call_fragments_sync(impu,
                                                           records_to_delete,
                                                           cass_timestamp++,
                                                           trail);
  if (rc != CassandraStore::OK)
  {
    // The delete failed - log this and don't retry
    TRC_ERROR("Deleting call list entries for IMPU: %s failed with rc %d",
                                                          impu.c_str(), rc);
  }
}

/// Determines whether the any call records need deleting from the call list
/// store
/// Requests the stored calls from Cassandra. If the number of stored calls
/// is too high, returns a timestamp to delete before to reduce the call
/// list length.
bool CallListStoreProcessor::Pool::is_call_trim_needed(
                    std::string impu,
                    std::vector<CallListStore::CallFragment>& records_to_delete,
                    SAS::TrailId trail)
{
  if (_max_call_list_length == 0)
  {
    // Don't perform any call list trimming if the max_call_list_length
    // option is set to 0
    return false;
  }

  // Check whether trimming is needed every 1 in (max_call_list_length / 10)
  // calls. Round up.
  int n = _max_call_list_length / 10;

  if (_max_call_list_length % 10 != 0)
  {
    n++;
  }

  if ((rand() % n) != 0)
  {
    return false; // LCOV_EXCL_LINE
  }

  bool call_trim_needed = false;

  Utils::StopWatch stop_watch;
  stop_watch.start();

  std::vector<CallListStore::CallFragment> records;
  CassandraStore::ResultCode rc = _call_list_store->get_call_fragments_sync(impu,
                                                                            records,
                                                                            trail);

  if (rc == CassandraStore::OK)
  {
    // Record the latency.
    unsigned long latency_us = 0;
    if (stop_watch.read(latency_us))
    {
      _call_list_store_proc->_stat_cassandra_read_latency.accumulate(latency_us);
    }

    // Call records successfully retrieved. Count how many BEGIN and
    // REJECTED entries there are (don't include END as this would double
    // count successful calls)
    int count = 0;
    for (std::vector<CallListStore::CallFragment>::const_iterator ii = records.begin();
         ii != records.end();
         ii++)
    {
      if ((ii->type == CallListStore::CallFragment::Type::BEGIN) ||
          (ii->type == CallListStore::CallFragment::Type::REJECTED))
      {
        count++;
      }
    }

    // If there are more stored calls than 110% of the maximum then we
    // need to delete some (110% is used so that the deletes can be
    // batched).
    if (count > (_max_call_list_length * 1.1))
    {
      int num_to_delete = count - _max_call_list_length;

      for (int ii = 0; ii != num_to_delete; ii++)
      {
        records_to_delete.push_back(records[ii]);
      }

      TRC_DEBUG("Need to remove %d calls entries", num_to_delete);

      SAS::Event event(trail, SASEvent::CALL_LIST_TRIM_NEEDED, 0);
      event.add_var_param(impu);
      event.add_static_param(count);
      event.add_static_param(_max_call_list_length);
      SAS::report_event(event);

      call_trim_needed = true;
    }
  }
  else
  {
    // The read failed - log this and don't retry
    TRC_ERROR("Reading call list entries for IMPU: %s failed with rc %d",
                                                              impu.c_str(), rc);
  }

  return call_trim_needed;
}

CallListStoreProcessor::Pool::Pool(CallListStoreProcessor* call_list_store_processor,
                                   CallListStore::Store* call_list_store,
                                   LoadMonitor* load_monitor,
                                   const int max_call_list_length,
                                   const int call_list_ttl,
                                   unsigned int num_threads,
                                   ExceptionHandler* exception_handler, 
                                   void (*callback)(CallListStoreProcessor::CallListRequest*),
                                   HttpNotifier* http_notifier,
                                   unsigned int max_queue) :
  ThreadPool<CallListStoreProcessor::CallListRequest*>(num_threads, 
                                                       exception_handler, 
                                                       callback, 
                                                       max_queue),
  _call_list_store(call_list_store),
  _load_monitor(load_monitor),
  _max_call_list_length(max_call_list_length),
  _call_list_ttl(call_list_ttl),
  _call_list_store_proc(call_list_store_processor),
  _http_notifier(http_notifier)
{}


CallListStoreProcessor::Pool::~Pool()
{}
