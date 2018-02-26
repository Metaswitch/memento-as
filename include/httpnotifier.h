/**
 * @file httpnotifier.h
 *
 * Copyright (C) Metaswitch Networks 2016
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
  HttpNotifier(HttpResolver* resolver, const std::string& notify_url);

  virtual ~HttpNotifier();

  /// This function sends a HTTP POST to the notify URL, to notify it of the
  /// fact that a call list for a user has updated. This is synchronous.
  /// Returns true iff the request was successful.
  /// @param impu       IMPU of the member whose call list has been updated
  /// @param trail      The SAS trail
  virtual bool send_notify(const std::string& impu, SAS::TrailId trail);

private:

  HttpResolver *_http_resolver;

  HttpClient *_http_client;

  HttpConnection *_http_connection;

  std::string _http_url_path;
};

#endif
