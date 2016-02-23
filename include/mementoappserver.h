/**
 * @file mementoappserver.h
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

#ifndef MEMENTOAPPSERVER_H__
#define MEMENTOAPPSERVER_H__

extern "C" {
#include <pjsip.h>
#include <pjsip/sip_msg.h>
#include <pjlib-util.h>
#include <pjlib.h>
}

#include "appserver.h"
#include "load_monitor.h"
#include "call_list_store_processor.h"
#include "sas.h"
#include "zmq_lvc.h"

class MementoAppServer;
class MementoAppServerTsx;

/// The MementoAppServer implements the Memento service, and subclasses
/// the abstract AppServer class.
///
/// Sprout calls the get_app_tsx method on MementoAppServer when
///
/// -  an IFC triggers with ServiceName containing a host name of the form
///    memento.<homedomain>;
/// -  a request is received for a dialog where the service previously called
///    add_to_dialog.
///
class MementoAppServer : public AppServer
{
public:
  /// Constructor.
  /// @param  service_name           - Service name (memento).
  /// @param  home_domain            - Home domain of deployment (from configuration).
  /// @param  max_call_list_length   - Maximum number of calls to store (from configuration).
  /// @param  memento_thread         - Number of memento threads (from configuration).
  /// @param  call_list_ttl          - Time to store calls in Cassandra (from configuration).
  /// @param  stats_aggregator       - Statistics aggregator (last value cache).
  /// @param  cass_target_latency_us - Target latency for the Cassandra requests (from configuration).
  /// @param  max_tokens             - Max number of tokens for the Cassandra load monitor (from configuration).
  /// @param  init_token_rate        - Initial token rate for the Cassandra load monitor (from configuration).
  /// @param  min_token_rate         - Minimum token rate for the Cassandra load monitor (from configuration).
  /// @param  http_resolver          - HTTP resolver to use for HTTP connections.
  /// @param  memento_notify_url     - HTTP URL that memento should notify when call lists change.
  MementoAppServer(const std::string& service_name,
                   CallListStore::Store* call_list_store,
                   const std::string& home_domain,
                   const int max_call_list_length,
                   const int memento_thread,
                   const int call_list_ttl,
                   LastValueCache* stats_aggregator,
                   const int cass_target_latency_us,
                   const int max_tokens,
                   const float init_token_rate,
                   const float min_token_rate,
                   ExceptionHandler* exception_handler,
                   HttpResolver* http_resolver,
                   const std::string& memento_notify_url);

  /// Virtual destructor.
  ~MementoAppServer();

  /// Called when the system determines the service should be invoked for a
  /// received request.  The AppServer can either return NULL indicating it
  /// does not want to process the request, or create a suitable object
  /// derived from the AppServerTsx class to process the request.
  ///
  /// @param  helper        - The service helper to use to perform
  ///                         the underlying service-related processing.
  /// @param  req           - The received request message.
  virtual AppServerTsx* get_app_tsx(AppServerTsxHelper* helper,
                                    pjsip_msg* req);

private:

  /// The name of this service.
  std::string _service_name;

  /// Home domain of deployment.
  std::string _home_domain;

  /// Load monitor.
  LoadMonitor* _load_monitor;

  /// HTTP notifier.
  HttpNotifier* _http_notifier;

  /// Call list store processor.
  CallListStoreProcessor* _call_list_store_processor;

  /// Statistic.
  StatisticCounter _stat_calls_not_recorded_due_to_overload;
};

/// The MementoAppServerTsx class subclasses AppServerTsx and provides
/// application-server-specific processing of a single transaction.  It
/// encapsulates a ServiceTsx, which it calls through to to perform the
/// underlying service-related processing.
///
class MementoAppServerTsx : public AppServerTsx
{
public:
  /// Virtual destructor.
  virtual ~MementoAppServerTsx();

  /// Called for an initial request (dialog-initiating or out-of-dialog) with
  /// the original received request for the transaction.
  /// This method stores information about the INVITE, and adds itself as a
  /// Record-Route header.
  /// @param req           - The received initial request.
  virtual void on_initial_request(pjsip_msg* req);

  /// Called for an in-dialog request with the original received request for
  /// the transaction.  On return the request will be forwarded within
  /// the dialog.
  /// @param req           - The received in-dialog request.
  virtual void on_in_dialog_request(pjsip_msg* req);

  /// Called with all responses received on the transaction.  If a transport
  /// error or transaction timeout occurs on a downstream leg, this method is
  /// called with a 408 response.  The return value indicates whether the
  /// response should be forwarded upstream (after suitable consolidation if
  /// the request was forked).  If the return value is false and new targets
  /// have been added with the add_target API, the original request is forked
  /// to them.
  ///
  /// @returns             - true if the response should be forwarded upstream
  ///                        false if the response should be dropped
  /// @param  rsp          - The received request.
  /// @param  fork_id      - The identity of the downstream fork on which
  ///                        the response was received.
  virtual void on_response(pjsip_msg* rsp, int fork_id);

  /// Constructor.
  MementoAppServerTsx(AppServerTsxHelper* helper,
                      CallListStoreProcessor* call_list_store_processor,
                      std::string& service_name,
                      std::string& home_domain);

private:
  /// Call list store processor.
  CallListStoreProcessor* _call_list_store_processor;

  /// The name of this service.
  std::string _service_name;

  /// Home domain of deployment.
  std::string _home_domain;

  /// Flag for whether the call is incoming or outgoing
  bool _outgoing;

  /// Start time of the call
  std::string _start_time_xml;
  std::string _start_time_cassandra;

  /// Caller name (can be empty)
  std::string _caller_name;

  /// Caller URI
  std::string _caller_uri;

  /// Callee name (can be empty)
  std::string _callee_name;

  /// Callee URI
  std::string _callee_uri;

  /// Answerer name (can be empty)
  std::string _answerer_name;

  /// Answerer URI
  std::string _answerer_uri;

  /// Flag for whether a response has already been received on this
  /// transaction
  bool _stored_entry;

  /// Unique identifier for this transaction - generated from the timestamp
  /// and a random number.
  std::string _unique_id;

  /// IMPU of the call list owner
  std::string _impu;

  /// Flag for whether this transaction includes the initial dialog request.
  bool _includes_initial_request;
};

/// Utility methods

/// Creates a formatted string from a tm
/// @param timestamp - The time to convert
/// @param pattern   - The format to use
std::string create_formatted_timestamp(tm* timestamp, const char* pattern);

// Converts a URI to a string
std::string uri_to_string(pjsip_uri_context_e context,
                          const pjsip_uri* uri);

// Converts a PJ_STR to a string
std::string pj_str_to_string(const pj_str_t* pjstr);

// Converts a string to a URI
pjsip_uri* uri_from_string(const std::string& uri_s,
                           pj_pool_t* pool,
                           pj_bool_t force_name_addr);

#endif
