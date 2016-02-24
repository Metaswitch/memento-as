/**
 * @file httpnotifier.cpp
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

#include "httpnotifier.h"

#include <rapidjson/document.h>
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

/// Constructor.
HttpNotifier::HttpNotifier(HttpResolver* resolver, const std::string& notify_url) :
  _http_resolver(resolver),
  _http_connection(NULL)
{
  std::string url_server;
  std::string url_path;
  if (Utils::parse_http_url(notify_url, url_server, url_path) &&
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
