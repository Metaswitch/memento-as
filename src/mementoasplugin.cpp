/**
 * @file mementoasplugin.cpp  Plug-in wrapper for the Memento Sproutlet.
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "cfgoptions.h"
#include "sproutletplugin.h"
#include "mementoappserver.h"
#include "call_list_store.h"
#include "sproutletappserver.h"
#include "memento_as_alarmdefinition.h"
#include "log.h"

class MementoPlugin : public SproutletPlugin
{
public:
  MementoPlugin();
  ~MementoPlugin();

  bool load(struct options& opt, std::list<Sproutlet*>& sproutlets);
  void unload();

private:
  CassandraResolver* _cass_resolver;
  CommunicationMonitor* _cass_comm_monitor;
  CallListStore::Store* _call_list_store;
  MementoAppServer* _memento;
  SproutletAppServerShim* _memento_sproutlet;
};

/// Export the plug-in using the magic symbol "sproutlet_plugin"
extern "C" {
MementoPlugin sproutlet_plugin;
}


MementoPlugin::MementoPlugin() :
  _cass_comm_monitor(NULL),
  _call_list_store(NULL),
  _memento(NULL),
  _memento_sproutlet(NULL)
{
}

MementoPlugin::~MementoPlugin()
{
}

/// Loads the Memento plug-in, returning the supported Sproutlets.
bool MementoPlugin::load(struct options& opt, std::list<Sproutlet*>& sproutlets)
{
  bool plugin_loaded = true;

  std::string memento_prefix = "";
  int memento_port = 0;
  std::string memento_uri = "";

  int memento_threads = 25;
  std::string memento_notify_url = "";
  int max_call_list_length = 0;
  int call_list_ttl = 604800;

  bool memento_enabled = true;
  std::string plugin_name = "memento";

  std::string cassandra = "localhost";

  //TJW2 TODO: Remove auto
  auto memento_it = opt.plugin_options.find(plugin_name);

  if (memento_it == opt.plugin_options.end())
  {
    TRC_STATUS("Memento options not specified on Sprout command. Memento disabled.");
    memento_enabled = false;
  }
  else
  {
    TRC_DEBUG("Got Memento options map");
    std::multimap<std::string, std::string>& memento_opts = memento_it->second;

    //TJW2 TODO: Remove auto
    auto prefix_it = memento_opts.find("prefix");

    if (prefix_it != memento_opts.end())
    {
      memento_prefix = prefix_it->second;
      TRC_DEBUG("Overriding prefix parameter: %s", memento_prefix.c_str());
    }

    //TJW2 TODO: Remove auto
    auto port_it = memento_opts.find("port");

    if (port_it != memento_opts.end())
    {
      memento_port = std::stoi(port_it->second);
      TRC_DEBUG("Set port parameter: %d", memento_port);
      if (memento_port == 0 )
      {
        TRC_STATUS("Memento port set to zero. Memento disabled.");
      }
      memento_enabled = (memento_port > 0);
    }
    else
    {
      TRC_STATUS("Memento port not set. Memento disabled.");
      memento_enabled = false;
    }

    //TJW2 TODO: Remove auto
    auto uri_it = memento_opts.find("uri");

    if (uri_it != memento_opts.end())
    {
      memento_uri = uri_it->second;
      TRC_DEBUG("Overriding uri parameter: %s", memento_uri.c_str());
    }

    //TJW2 TODO: Remove auto
    auto threads_it = memento_opts.find("threads");

    if (threads_it != memento_opts.end())
    {
      memento_threads = std::stoi(threads_it->second);
      TRC_DEBUG("Set threads parameter: %d", memento_threads);
    }
    else
    {
      TRC_STATUS("Memento thread parameter not set. Memento disabled.");
      memento_enabled = false;
    }

    //TJW2 TODO: Remove auto
    auto url_it = memento_opts.find("notify_url");

    if (url_it != memento_opts.end())
    {
      memento_notify_url = std::stoi(url_it->second);
      TRC_DEBUG("Set notify_url: %s", memento_notify_url.c_str());
    }
    else
    {
      TRC_STATUS("Memento notify_url not set. Memento disabled.");
      memento_enabled = false;
    }

    // If the memento cassandra hostname option is set, use that instead of "localhost".
    //TJW2 TODO: Remove auto
    auto cassandra_it = memento_opts.find("cassandra");

    if (cassandra_it != memento_opts.end())
    {
      cassandra = cassandra_it->second;
      TRC_DEBUG("Set cassandra hostname: %s", cassandra.c_str());
    }

  }

  if (memento_enabled)
  {
    TRC_STATUS("Memento plugin enabled");

    SNMP::SuccessFailCountByRequestTypeTable* incoming_sip_transactions_tbl = SNMP::SuccessFailCountByRequestTypeTable::create("memento_as_incoming_sip_transactions",
                                                                                                                               "1.2.826.0.1.1578918.9.8.1.4");
    SNMP::SuccessFailCountByRequestTypeTable* outgoing_sip_transactions_tbl = SNMP::SuccessFailCountByRequestTypeTable::create("memento_as_outgoing_sip_transactions",
                                                                                                                               "1.2.826.0.1.1578918.9.8.1.5");
    if (((max_call_list_length == 0) &&
         (call_list_ttl == 0)))
    {
      TRC_ERROR("Can't have an unlimited maximum call length and a unlimited TTL for the call list store - disabling Memento");
    }
    else
    {
      _cass_comm_monitor = new CommunicationMonitor(new Alarm(alarm_manager,
                                                              "memento",
                                                              AlarmDef::MEMENTO_AS_CASSANDRA_COMM_ERROR,
                                                              AlarmDef::CRITICAL),
                                                    "Memento",
                                                    "Memcached");

      // We need the address family for the CassandraResolver
      int af = AF_INET;
      struct in6_addr dummy_addr;
      if (inet_pton(AF_INET6, opt.local_host.c_str(), &dummy_addr) == 1)
      {
        TRC_DEBUG("Local host is an IPv6 address");
        af = AF_INET6;
      }

      // Default to a 30s blacklist/graylist duration and port 9160
      _cass_resolver = new CassandraResolver(dns_resolver,
                                             af,
                                             30,
                                             30,
                                             9160);

      _call_list_store = new CallListStore::Store();
      _call_list_store->configure_connection(cassandra, 9160, _cass_comm_monitor, _cass_resolver);

      _memento = new MementoAppServer(memento_prefix,
                                      _call_list_store,
                                      opt.home_domain,
                                      max_call_list_length,
                                      memento_threads,
                                      call_list_ttl,
                                      stack_data.stats_aggregator,
                                      opt.cass_target_latency_us,
                                      opt.max_tokens,
                                      opt.init_token_rate,
                                      opt.min_token_rate,
                                      opt.max_token_rate,
                                      exception_handler,
                                      http_resolver,
                                      memento_notify_url);

      _memento_sproutlet = new SproutletAppServerShim(_memento,
                                                      memento_port,
                                                      memento_uri,
                                                      incoming_sip_transactions_tbl,
                                                      outgoing_sip_transactions_tbl);
      sproutlets.push_back(_memento_sproutlet);
    }
  }

  return plugin_loaded;
}

/// Unloads the Memento plug-in.
void MementoPlugin::unload()
{
  delete _memento_sproutlet;
  delete _memento;
  delete _cass_resolver;
  delete _call_list_store;
  delete _cass_comm_monitor;
}
