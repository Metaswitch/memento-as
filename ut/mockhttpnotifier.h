/**
 * @file mockhttpnotifier.h Mock HttpNotifier object
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCKHTTPNOTIFIER_H_
#define MOCKHTTPNOTIFIER_H_

#include <gmock/gmock.h>

#include "httpnotifier.h"

class MockHttpNotifier : public HttpNotifier
{
public:
  MockHttpNotifier();
  virtual ~MockHttpNotifier();

  MOCK_METHOD2(send_notify,
               bool(const std::string& impu,
                    SAS::TrailId triail));
};

#endif

