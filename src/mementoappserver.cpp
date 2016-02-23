/**
 * @file mementoappserver.cpp
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
#include "mementoappserver.h"
#include "call_list_store_processor.h"
#include "httpnotifier.h"
#include "log.h"
#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_print.hpp"
#include "base64.h"
#include <ctime>
#include "utils.h"
#include "mementosasevent.h"

static const int MAX_CALL_ENTRY_LENGTH = 4096;
static const char* TIMESTAMP_PATTERN = "%Y%m%d%H%M%S";
static const char* XML_PATTERN = "%Y-%m-%dT%H:%M:%S";

static const pj_str_t ORIG = pj_str((char*)"orig");
static const pj_str_t ORIG_CDIV = pj_str((char*)"orig-cdiv");
static const pj_str_t P_SERVED_USER = pj_str((char*)"P-Served-User");
static const pj_str_t P_ASSERTED_IDENTITY = pj_str((char*)"P-Asserted-Identity");
static const pj_str_t SESCASE = pj_str((char*)"sescase");

// Constants to create the Call list XML
namespace MementoXML
{
  static const char* TO = "to";
  static const char* FROM = "from";
  static const char* NAME = "name";
  static const char* URI = "URI";
  static const char* OUTGOING = "outgoing";
  static const char* ANSWERED = "answered";
  static const char* ANSWERER = "answerer";
  static const char* START_TIME = "start-time";
  static const char* END_TIME = "end-time";
  static const char* ANSWER_TIME = "answer-time";
}

/// Constructor.
MementoAppServer::MementoAppServer(const std::string& service_name,
                                   CallListStore::Store* call_list_store,
                                   const std::string& home_domain,
                                   const int max_call_list_length,
                                   const int memento_threads,
                                   const int call_list_ttl,
                                   LastValueCache* stats_aggregator,
                                   const int cass_target_latency,
                                   const int max_tokens,
                                   const float init_token_rate,
                                   const float min_token_rate,
                                   ExceptionHandler* exception_handler,
                                   HttpResolver* http_resolver,
                                   const std::string& memento_notify_url) :
  AppServer(service_name),
  _service_name(service_name),
  _home_domain(home_domain),
  _load_monitor(new LoadMonitor(cass_target_latency,
                                max_tokens,
                                init_token_rate,
                                min_token_rate)),
  _http_notifier(new HttpNotifier(http_resolver, memento_notify_url)),
  _call_list_store_processor(new CallListStoreProcessor(_load_monitor,
                                                        call_list_store,
                                                        max_call_list_length,
                                                        memento_threads,
                                                        call_list_ttl,
                                                        stats_aggregator,
                                                        exception_handler,
                                                        _http_notifier)),
  _stat_calls_not_recorded_due_to_overload("memento_not_recorded_overload",
                                           stats_aggregator)
{
}

/// Destructor.
MementoAppServer::~MementoAppServer()
{
  delete _load_monitor; _load_monitor = NULL;
  delete _call_list_store_processor; _call_list_store_processor = NULL;
  delete _http_notifier; _http_notifier = NULL;
}

// Returns an AppServerTsx if the load monitor admits the request, and if
// the request is either an INVITE or a BYE.
AppServerTsx* MementoAppServer::get_app_tsx(AppServerTsxHelper* helper,
                                            pjsip_msg* req)
{
  if ((req->line.req.method.id != PJSIP_INVITE_METHOD) &&
      (req->line.req.method.id != PJSIP_BYE_METHOD))
  {
    // Request isn't an INVITE or BYE, no processing is required.
    return NULL;
  }

  // Check for available tokens on the initial request
  if ((req->line.req.method.id == PJSIP_INVITE_METHOD) &&
      (!_load_monitor->admit_request()))
  {
    // LCOV_EXCL_START
    TRC_WARNING("No available tokens - no memento processing of request");
    SAS::Event event(helper->trail(), SASEvent::CALL_LIST_OVERLOAD, 0);
    SAS::report_event(event);
    _stat_calls_not_recorded_due_to_overload.increment();
    return NULL;
    // LCOV_EXCL_STOP
  }

  TRC_DEBUG("Getting a MementoAppServerTsx");

  MementoAppServerTsx* memento_tsx =
                    new MementoAppServerTsx(helper,
                                            _call_list_store_processor,
                                            _service_name,
                                            _home_domain);
  return memento_tsx;
}

// Constructor
MementoAppServerTsx::MementoAppServerTsx(
                     AppServerTsxHelper* helper,
                     CallListStoreProcessor* call_list_store_processor,
                     std::string& service_name,
                     std::string& home_domain) :
    AppServerTsx(helper),
    _call_list_store_processor(call_list_store_processor),
    _service_name(service_name),
    _home_domain(home_domain),
    _outgoing(false),
    _start_time_xml(""),
    _start_time_cassandra(""),
    _caller_name(""),
    _caller_uri(""),
    _callee_name(""),
    _callee_uri(""),
    _answerer_name(""),
    _answerer_uri(""),
    _stored_entry(false),
    _unique_id(""),
    _impu(""),
    _includes_initial_request(false)
{
}

// Destructor
MementoAppServerTsx::~MementoAppServerTsx() {}

void MementoAppServerTsx::on_initial_request(pjsip_msg* req)
{
  TRC_DEBUG("Memento processing an initial request of type %s",
           (req->line.req.method.id == PJSIP_INVITE_METHOD) ? "INVITE" : "BYE");

  // Mark that memento should care about this transaction's response.
  _includes_initial_request = true;

  // Get the current time
  time_t rawtime;
  time(&rawtime);
  tm* start_time = localtime(&rawtime);
  _start_time_xml = create_formatted_timestamp(start_time, XML_PATTERN);
  _start_time_cassandra = create_formatted_timestamp(start_time, TIMESTAMP_PATTERN);

  // Is the call originating or terminating?
  std::string served_user;
  pjsip_routing_hdr* psu_hdr = (pjsip_routing_hdr*)
                     pjsip_msg_find_hdr_by_name(req, &P_SERVED_USER, NULL);

  if (psu_hdr != NULL)
  {
    pjsip_uri* uri = (pjsip_uri*)pjsip_uri_get_uri(&psu_hdr->name_addr);
    served_user = uri_to_string(PJSIP_URI_IN_ROUTING_HDR, uri);

    pjsip_param* sescase = pjsip_param_find(&psu_hdr->other_param, &SESCASE);
    pjsip_param* orig_cdiv = pjsip_param_find(&psu_hdr->other_param, &ORIG_CDIV);

    if (((sescase != NULL) &&
         (pj_stricmp(&sescase->value, &ORIG) == 0)) ||
        (orig_cdiv != NULL))
    {
      // orig-cdiv is treated as originating. Note that this means the served
      // subscriber will see this call in their call logs, despite their endpoint
      // not being involved in this call. Because of this, we don't recommend
      // invoking memento on orig-cdiv in their IFCs.
      TRC_DEBUG("Request is originating");

      _outgoing = true;
    }
  }

  // Get the caller, callee and impu values
  if (_outgoing)
  {
    // Get the callee's URI amd name from the To header.
    _callee_uri = uri_to_string(PJSIP_URI_IN_FROMTO_HDR,
                    (pjsip_uri*)pjsip_uri_get_uri(PJSIP_MSG_TO_HDR(req)->uri));
    _callee_name = pj_str_to_string(&((pjsip_name_addr*)
                                       (PJSIP_MSG_TO_HDR(req)->uri))->display);

    // Get the caller's URI and name from the P-Asserted Identity header. If
    // this is missing, use the From header.
    pjsip_routing_hdr* asserted_id = (pjsip_routing_hdr*)
               pjsip_msg_find_hdr_by_name(req, &P_ASSERTED_IDENTITY, NULL);

    if (asserted_id != NULL)
    {
      _caller_uri = uri_to_string(PJSIP_URI_IN_FROMTO_HDR,
                       (pjsip_uri*)pjsip_uri_get_uri(&asserted_id->name_addr));
      _caller_name = pj_str_to_string(&asserted_id->name_addr.display);
    }
    else
    {
      TRC_WARNING("INVITE missing P-Asserted-Identity");
      send_request(req);
      return;
    }

    // Set the IMPU equal to the caller's URI
    _impu = _caller_uri;
  }
  else
  {
    // Get the callee's URI from the request URI. There can be no name value.
    _callee_uri =  uri_to_string(PJSIP_URI_IN_FROMTO_HDR, req->line.req.uri);

    // Get the caller's URI and name from the From header.
    _caller_uri = uri_to_string(PJSIP_URI_IN_FROMTO_HDR,
                (pjsip_uri*)pjsip_uri_get_uri(PJSIP_MSG_FROM_HDR(req)->uri));
    _caller_name = pj_str_to_string(&((pjsip_name_addr*)
                                   (PJSIP_MSG_FROM_HDR(req)->uri))->display);

    // Set the IMPU equal to the callee's URI
    _impu = _callee_uri;
  }

  // Add a unique ID containing the IMPU to the record route header.
  // This has the format:
  //     <YYYYMMDDHHMMSS>_<unique_id>_<base64 encoded impu>.memento.<home domain>
  _unique_id = std::to_string(Utils::generate_unique_integer(0,0));
  std::string encoded_impu =
     base64_encode(reinterpret_cast<const unsigned char*>(_impu.c_str()),
                                                          _impu.length());
  std::string dialog_id = std::string(_start_time_cassandra).
                          append("_").
                          append(_unique_id).
                          append("_").
                          append(encoded_impu);

  add_to_dialog(dialog_id);
  send_request(req);
}

void MementoAppServerTsx::on_in_dialog_request(pjsip_msg* req)
{
  TRC_DEBUG("Mememto processing an in_dialog_request");

  if (req->line.req.method.id != PJSIP_BYE_METHOD)
  {
    // Request isn't a BYE request, e.g. it's a reINVITE. Do nothing.
    send_request(req);
    return;
  }

  // Get the dialog id. It has the format:
  //  <YYYYMMDDHHMMSS>_<unique_id>_<base64 encoded impu>
  std::string dialogid = dialog_id();

  // Pull out the timestamp, id and IMPU.
  std::vector<std::string> dialog_values;
  Utils::split_string(dialogid, '_', dialog_values, 3, false);

  if (dialog_values.size() != 3)
  {
    // LCOV_EXCL_START
    TRC_WARNING("Invalid dialog ID (%s)", dialogid.c_str());
    send_request(req);
    return;
    // LCOV_EXCL_STOP
  }

  std::string timestamp = dialog_values[0];
  _unique_id = dialog_values[1];
  _impu = base64_decode(dialog_values[2]);

  // Create the XML. XML should be of the form:
  //   <end-time><current time></end-time>

  // Get the current time
  time_t currenttime;
  time(&currenttime);
  tm* ct = localtime(&currenttime);

  // Create the XML
  rapidxml::xml_document<> doc;
  std::string end_timestamp = create_formatted_timestamp(ct, XML_PATTERN);

  rapidxml::xml_node<>* root = doc.allocate_node(
                                  rapidxml::node_element,
                                  MementoXML::END_TIME,
                                  doc.allocate_string(end_timestamp.c_str()));
  doc.append_node(root);

  char contents[MAX_CALL_ENTRY_LENGTH] = {0};
  char* end = rapidxml::print(contents, doc);
  *end = 0;

  // Write the call list entry to the call list store.
  SAS::Event event(trail(), SASEvent::CALL_LIST_END_FRAGMENT, 0);
  SAS::report_event(event);

  _call_list_store_processor->write_call_list_entry(
                                        _impu,
                                        timestamp,
                                        _unique_id,
                                        CallListStore::CallFragment::Type::END,
                                        contents,
                                        trail());

  send_request(req);
}

void MementoAppServerTsx::on_response(pjsip_msg* rsp, int fork_id)
{
  TRC_DEBUG("Memento processing a response");

  pjsip_cseq_hdr* cseq = (pjsip_cseq_hdr*)pjsip_msg_find_hdr(rsp,
                                                             PJSIP_H_CSEQ,
                                                             NULL);

  if (cseq == NULL || cseq->method.id != PJSIP_INVITE_METHOD || !_includes_initial_request)
  {
    // Response isn't for the initial INVITE, do nothing
    send_response(rsp);
    return;
  }

  if (_stored_entry)
  {
    TRC_DEBUG("Already received a final response, no further processing");
    send_response(rsp);
    return;
  }

  if (rsp->line.status.code < 200)
  {
    // Non-final response; do nothing
    send_response(rsp);
    return;
  }
  else
  {
     _stored_entry = true;
  }

  // Create the XML. The XML should have the format:
  //  <to>
  //    <URI>_callee_uri</URI>
  //    <name>_callee_name</name> - may be absent
  //  </to>
  //  <from>
  //    <URI>_caller_uri</URI>
  //    <name>_caller_name</name> - may be absent
  //  </from>
  //  <answered>_answered</answered>
  //  <outgoing>_outgoing</outgoing>
  //  <start-time>_start_time</start-time>
  //  <answer-time><current time></answer-time> - Only present if
  //                                              call was answered
  //  <answerer> - may be absent
  //    <URI>_answerer_uri</URI>
  //    <name>_answerer_name</name> - may be absent
  //  </answerer>
  rapidxml::xml_document<> doc;

  // Fill in the 'to' values from the callee values.
  rapidxml::xml_node<>* to = doc.allocate_node(rapidxml::node_element,
                                               MementoXML::TO);
  rapidxml::xml_node<>* to_uri = doc.allocate_node(
                                      rapidxml::node_element,
                                      MementoXML::URI,
                                      doc.allocate_string(_callee_uri.c_str()));
  to->append_node(to_uri);

  if (_callee_name != "")
  {
    rapidxml::xml_node<>* to_name = doc.allocate_node(
                                      rapidxml::node_element,
				      MementoXML::NAME,
                                      doc.allocate_string(_callee_name.c_str()));
    to->append_node(to_name);
  }

  doc.append_node(to);

  // Fill in the 'from' values from the caller values.
  rapidxml::xml_node<>* from = doc.allocate_node(rapidxml::node_element,
                                                 MementoXML::FROM);
  rapidxml::xml_node<>* from_uri = doc.allocate_node(
                                        rapidxml::node_element,
					MementoXML::URI,
                                        doc.allocate_string(_caller_uri.c_str()));
  from->append_node(from_uri);

  if (_caller_name != "")
  {
    rapidxml::xml_node<>* from_name = doc.allocate_node(
                                        rapidxml::node_element,
					MementoXML::NAME,
					doc.allocate_string(_caller_name.c_str()));
    from->append_node(from_name);
  }

  doc.append_node(from);

  // Set outgoing to 1 if the call is outgoing, and 0 otherwise.
  std::string outgoing_str = _outgoing ? "1" : "0";
  rapidxml::xml_node<>* outgoing = doc.allocate_node(
                                         rapidxml::node_element,
					 MementoXML::OUTGOING,
                                         doc.allocate_string(outgoing_str.c_str()));
  doc.append_node(outgoing);

  // Set the start time.
  rapidxml::xml_node<>* start_time = doc.allocate_node(
                                      rapidxml::node_element,
                                      MementoXML::START_TIME,
                                      doc.allocate_string(_start_time_xml.c_str()));
  doc.append_node(start_time);

  CallListStore::CallFragment::Type type;

  if (rsp->line.status.code >= 300)
  {
    // If the call was rejected, set answered to 0.
    rapidxml::xml_node<>* answered = doc.allocate_node(rapidxml::node_element,
                                                       MementoXML::ANSWERED,
                                                       "0");
    doc.append_node(answered);
    type = CallListStore::CallFragment::Type::REJECTED;
  }
  else
  {
    // If the call was answered, set answered to 1. Also fill in the answer time
    // with the current time.
    rapidxml::xml_node<>* answered = doc.allocate_node(rapidxml::node_element,
                                                       MementoXML::ANSWERED,
                                                       "1");
    doc.append_node(answered);

    time_t currenttime;
    time(&currenttime);
    tm* ct = localtime(&currenttime);
    std::string answer_timestamp = create_formatted_timestamp(ct, XML_PATTERN);
    rapidxml::xml_node<>* answer_time = doc.allocate_node(
                                      rapidxml::node_element,
                                      MementoXML::ANSWER_TIME,
                                      doc.allocate_string(answer_timestamp.c_str()));
    doc.append_node(answer_time);

    // Also, pick up the answerer from the P-A-I header, as long as the
    // responder hasn't requested this to be private.  Look for the 'id'
    // value in the Privacy header.
    bool privacy_requested = false;
    pj_str_t privacy_hdr_name = pj_str(const_cast<char*>("Privacy"));
    pjsip_generic_string_hdr* privacy = (pjsip_generic_string_hdr*)
                    pjsip_msg_find_hdr_by_name(rsp, &privacy_hdr_name, NULL);

    if (privacy)
    {
      if (!pj_stricmp2(&privacy->hvalue, "id"))
      {
        // Responder has requested ID to be private.
        privacy_requested = true;
      }
    }

    if (!privacy_requested)
    {
      pjsip_routing_hdr* asserted_id = (pjsip_routing_hdr*)
                 pjsip_msg_find_hdr_by_name(rsp, &P_ASSERTED_IDENTITY, NULL);

      if (asserted_id != NULL)
      {
        _answerer_uri = uri_to_string(PJSIP_URI_IN_FROMTO_HDR,
                         (pjsip_uri*)pjsip_uri_get_uri(&asserted_id->name_addr));
        _answerer_name = pj_str_to_string(&asserted_id->name_addr.display);

        // Fill in the 'answerer' values from the answerer values.
        rapidxml::xml_node<>* answerer = doc.allocate_node(rapidxml::node_element,
                                                           MementoXML::ANSWERER);
        rapidxml::xml_node<>* answerer_uri = doc.allocate_node(
                                       rapidxml::node_element,
                                       MementoXML::URI,
                                       doc.allocate_string(_answerer_uri.c_str()));
        answerer->append_node(answerer_uri);

        if (!_answerer_name.empty())
        {
          rapidxml::xml_node<>* answerer_name = doc.allocate_node(
                                      rapidxml::node_element,
                                      MementoXML::NAME,
                                      doc.allocate_string(_answerer_name.c_str()));
          answerer->append_node(answerer_name);
        }

        doc.append_node(answerer);
      }
    }

    type = CallListStore::CallFragment::Type::BEGIN;
  }

  char contents[MAX_CALL_ENTRY_LENGTH] = {0};
  char* end = rapidxml::print(contents, doc);
  *end = 0;

  // Log to SAS
  if (type == CallListStore::CallFragment::Type::BEGIN)
  {
    SAS::Event event(trail(), SASEvent::CALL_LIST_BEGIN_FRAGMENT, 0);
    SAS::report_event(event);
  }
  else
  {
    SAS::Event event(trail(), SASEvent::CALL_LIST_REJECTED_FRAGMENT, 0);
    SAS::report_event(event);
  }

  // Write the XML to cassandra (using a different thread)
  _call_list_store_processor->write_call_list_entry(_impu,
                                                    _start_time_cassandra,
                                                    _unique_id,
                                                    type,
                                                    contents,
                                                    trail());

  send_response(rsp);
}

// Utility methods
std::string create_formatted_timestamp(tm* timestamp,
                                       const char* pattern)
{
  char formatted_time[80];
  std::strftime(formatted_time, sizeof(formatted_time), pattern, timestamp);
  return std::string(formatted_time);
}

std::string uri_to_string(pjsip_uri_context_e context,
                          const pjsip_uri* uri)
{
  int uri_clen = 0;
  char uri_cstr[500];

  if (uri != NULL)
  {
    uri_clen = pjsip_uri_print(context, uri, uri_cstr, sizeof(uri_cstr));
  }

  return std::string(uri_cstr, uri_clen);
}

std::string pj_str_to_string(const pj_str_t* pjstr)
{
  return ((pjstr != NULL) && (pj_strlen(pjstr) > 0)) ?
              std::string(pj_strbuf(pjstr), pj_strlen(pjstr)) : std::string("");
}

pjsip_uri* uri_from_string(const std::string& uri_s,
                           pj_pool_t* pool,
                           pj_bool_t force_name_addr)
{
  size_t len = uri_s.length();
  char* buf = (char*)pj_pool_alloc(pool, len + 1);
  memcpy(buf, uri_s.data(), len);
  buf[len] = 0;

  return pjsip_parse_uri(pool, buf, len, (force_name_addr) ?
                                               PJSIP_PARSE_URI_AS_NAMEADDR : 0);
}
