/**
 * @file httpnotifier.h
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef HTTPNOTIFIER_H__
#define HTTPNOTIFIER_H__

#include "httpconnection.h"
#include "sas.h"

class HttpNotifier
{
public:
  /// Constructor
  HttpNotifier(HttpResolver* resolver, const std::string& notify_url);

  /// Destructor
  virtual ~HttpNotifier();

  /// This function sends a HTTP POST to the notify URL, to notify it of the
  /// fact that a call list for a user has updated. This is synchronous.
  /// Returns true iff the request was successful.
  /// @param impu       IMPU of the member whose call list has been updated
  /// @param trail      The SAS trail
  virtual bool send_notify(const std::string& impu, SAS::TrailId trail);

private:

  /// Resolver
  HttpResolver *_http_resolver;

  /// Connection
  HttpConnection *_http_connection;

  /// URL path
  std::string _http_url_path;
};

#endif
