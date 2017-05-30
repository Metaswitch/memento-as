/**
 * @file httpnotifier.cpp
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "httpnotifier.h"

#include <rapidjson/document.h>
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

/// Constructor.
HttpNotifier::HttpNotifier(HttpResolver* resolver, const std::string& notify_url) :
  _http_resolver(resolver),
  _http_connection(NULL)
{
  std::string url_scheme;
  std::string url_server;
  std::string url_path;
  if (Utils::parse_http_url(notify_url, url_scheme, url_server, url_path) &&
      !url_server.empty())
  {
    _http_connection = new HttpConnection(url_server,
                                          true,
                                          _http_resolver,
                                          SASEvent::HttpLogLevel::PROTOCOL,
                                          NULL);
    _http_url_path = url_path;
  }
}

/// Destructor.
HttpNotifier::~HttpNotifier()
{
  if (_http_connection != NULL)
  {
    delete _http_connection; _http_connection = NULL;
  }
}

/// Notify that a subscriber's call list has changed
bool HttpNotifier::send_notify(const std::string& impu,
                               SAS::TrailId trail)
{
  if (_http_connection == NULL)
  {
    // No notifier attached.  Do nothing.
    return true;
  }

  rapidjson::Document notification;
  notification.SetObject();
  rapidjson::Value impu_value;
  impu_value.SetString(impu.c_str(), notification.GetAllocator());
  notification.AddMember("impu", impu_value, notification.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  notification.Accept(writer);

  std::map<std::string, std::string> headers;
  std::string body = buffer.GetString();

  HTTPCode http_code = _http_connection->send_post(_http_url_path,
                                                   headers,
                                                   body,
                                                   trail);
  return (http_code == HTTP_OK);
}
