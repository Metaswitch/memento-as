/**
 * @file call_list_store_processor.h
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef CALL_LIST_STORE_PROCESSOR_H_
#define CALL_LIST_STORE_PROCESSOR_H_

#include "call_list_store.h"
#include "threadpool.h"
#include "load_monitor.h"
#include "sas.h"
#include "mementosasevent.h"
#include "counter.h"
#include "accumulator.h"
#include "httpnotifier.h"

class CallListStoreProcessor
{
public:
  /// Constructor
  CallListStoreProcessor(LoadMonitor* load_monitor,
                         CallListStore::Store* call_list_store,
                         const int max_call_list_length,
                         const int memento_thread,
                         const int call_list_ttl,
                         LastValueCache* stats_aggregator,
                         ExceptionHandler* exception_handler,
                         HttpNotifier* notifier);

  /// Destructor
  virtual ~CallListStoreProcessor();

  /// This function constructs a Cassandra request to write a call to the
  /// call list store. It runs synchronously, so must be done in a
  /// separate thread to avoid introducing unnecessary latencies in the
  /// call path.
  /// @param impu       IMPU
  /// @param timestamp  Timestamp of call list entry
  /// @param id         Id of call list entry
  /// @param type       Type of call fragment to write
  /// @param xml        Contents of call list entry
  /// @param trail      SAS trail
  virtual void write_call_list_entry(std::string impu,
                                     std::string timestamp,
                                     std::string id,
                                     CallListStore::CallFragment::Type type,
                                     std::string xml,
                                     SAS::TrailId trail);

  struct CallListRequest
  {
    Utils::StopWatch stop_watch;
    std::string impu;
    std::string timestamp;
    std::string id;
    CallListStore::CallFragment::Type type;
    std::string contents;
    SAS::TrailId trail;
  };

  static void exception_callback(CallListStoreProcessor::CallListRequest* work)
  {
    // No recovery behaviour as this is asynchronos, so we can't sensibly
    // respond
  }

private:
  /// @class Pool
  /// The thread pool used by the call list store processor
  class Pool : public ThreadPool<CallListStoreProcessor::CallListRequest*>
  {
  public:
    /// Constructor.
    /// @param call_list_store_proc Parent call list store processor.
    /// @param call_list_store      A pointer to the underlying call list store.
    /// @param load_monitor         A pointer to the load monitor.
    /// @param max_call_list_length Maximum number of complete calls to store
    /// @param call_list_ttl        TTL of call list store entries.
    /// @param num_threads          Number of memento worker threads to start
    /// @param max_queue            Max queue size to allow.
    Pool(CallListStoreProcessor* call_list_store_proc,
         CallListStore::Store* call_list_store,
         LoadMonitor* load_monitor,
         const int max_call_list_length,
         const int call_list_ttl,
         unsigned int num_threads,
         ExceptionHandler* exception_handler,
         void (*callback)(CallListStoreProcessor::CallListRequest* work),
         HttpNotifier* http_notifier,
         unsigned int max_queue = 0);

    /// Destructor
    virtual ~Pool();

  private:
    /// Called by worker threads when they pull work off the queue.
    virtual void process_work(CallListStoreProcessor::CallListRequest*&);

    /// Performs call trim processing
    /// @param impu            IMPU.
    /// @param fragments       (out) fragments to delete
    /// @param cass_timestamp  Cassandra timestamp
    /// @param trail           SAS trail
    void perform_call_trim(std::string impu,
                           std::vector<CallListStore::CallFragment>& fragments,
                           uint64_t cass_timestamp,
                           SAS::TrailId trail);

    /// Works out if a trim is needed to reduce the length of an IMPU's
    /// call list. If it is required, this function also outputs the cut
    /// off time.
    /// @param impu            IMPU.
    /// @param fragments       (out) Fragments to be deleted
    /// @param trail           SAS trail
    bool is_call_trim_needed(std::string impu,
                             std::vector<CallListStore::CallFragment>& fragments,
                             SAS::TrailId trail);

    /// Underlying call list store
    CallListStore::Store* _call_list_store;

    /// Load monitor
    LoadMonitor* _load_monitor;

    /// Maximum number of calls to store.
    int _max_call_list_length;

    /// Time to store calls in Cassandra.
    int _call_list_ttl;

    /// Parent call list store processor.
    CallListStoreProcessor* _call_list_store_proc;

    /// Notifier
    HttpNotifier* _http_notifier;
  };

  friend class Pool;

  ///  Thread pool
  Pool* _thread_pool;

  // Statistics.
  StatisticCounter _stat_completed_calls_recorded;
  StatisticCounter _stat_failed_calls_recorded;
  StatisticAccumulator _stat_cassandra_read_latency;
  StatisticAccumulator _stat_cassandra_write_latency;
};

#endif
