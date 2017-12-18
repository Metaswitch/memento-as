ROOT := $(abspath $(shell pwd)/../../..)
MODULE_DIR := ${ROOT}/modules

TARGETS := memento-as.so
TEST_TARGETS := memento-as_test

MEMENTO_AS_COMMON_SOURCES := mementoasplugin.cpp \
                             call_list_store.cpp \
                             call_list_store_processor.cpp \
                             mementoappserver.cpp \
                             mementosaslogger.cpp

memento-as.so_SOURCES := ${MEMENTO_AS_COMMON_SOURCES}

memento-as_test_SOURCES := ${MEMENTO_AS_COMMON_SOURCES} \
                           httpnotifier.cpp \
                           httpconnection.cpp \
                           httpclient.cpp \
                           logger.cpp \
                           saslogger.cpp \
                           accesslogger.cpp \
                           log.cpp \
                           sas.cpp \
                           http_connection_pool.cpp \
                           exception_handler.cpp \
                           health_checker.cpp \
                           counter.cpp \
                           accumulator.cpp \
                           unique.cpp \
                           utils.cpp \
                           custom_headers.cpp \
                           namespace_hop.cpp \
                           sip_common.cpp \
                           faketransport_tcp.cpp \
                           pjutils.cpp \
                           statistic.cpp \
                           zmq_lvc.cpp \
                           acr.cpp \
                           uri_classifier.cpp \
                           stack.cpp \
                           quiescing_manager.cpp \
                           connection_tracker.cpp \
                           baseresolver.cpp \
                           dnscachedresolver.cpp \
                           sipresolver.cpp \
                           dnsparser.cpp \
                           base64.cpp \
                           thread_dispatcher.cpp \
                           cassandra_connection_pool.cpp \
                           cassandra_store.cpp \
                           load_monitor.cpp \
                           httpstack.cpp \
                           test_main.cpp \
                           test_interposer.cpp \
                           mementoappserver_test.cpp

COMMON_CPPFLAGS := -I${ROOT}/include \
                   -I${ROOT}/usr/include \
                   -I${ROOT}/modules/cpp-common/include \
                   -I${ROOT}/modules/sas-client/include \
                   -I${ROOT}/modules/app-servers/include \
                   -I${ROOT}/modules/rapidjson/include \
                   -I${ROOT}/plugins/memento-as/modules/memento-common/include \
                   -I${ROOT}/plugins/memento-as/include \
                   -Wno-write-strings \
                   -fPIC

memento-as.so_CPPFLAGS := ${COMMON_CPPFLAGS}

memento-as_test_CPPFLAGS := ${COMMON_CPPFLAGS} \
                            -Wno-write-strings \
                            -I${ROOT}/modules/cpp-common/test_utils \
                            -I${ROOT}/modules/app-servers/test \
                            -I${ROOT}/src/ut \
                            `PKG_CONFIG_PATH=${ROOT}/usr/lib/pkgconfig pkg-config --cflags libpjproject` \
                            -DGTEST_USE_OWN_TR1_TUPLE=0

VPATH = ${ROOT}/src:${ROOT}/modules/cpp-common/src:${ROOT}/plugins/memento-as/src:${ROOT}/plugins/memento-as/src/ut:${ROOT}/modules/cpp-common/test_utils:${ROOT}/src/ut:${ROOT}/plugins/memento-as/modules/memento-common/src:${ROOT}/modules/sas-client/source

COMMON_LDFLAGS := -L${ROOT}/usr/lib \
                  -lthrift \
                  -lcassandra

memento-as.so_LDFLAGS := ${COMMON_LDFLAGS} \
                         -shared

memento-as_test_LDFLAGS := ${COMMON_LDFLAGS} \
                           -levent \
                           -levhtp \
                           -lsas \
                           -lcares \
                           -levent_pthreads \
                           -lboost_regex \
                           -lpthread \
                           -ldl \
                           -lboost_system \
                           -lcurl \
                           -lzmq \
                           -lz \
                           $(shell PKG_CONFIG_PATH=${ROOT}/usr/lib/pkgconfig pkg-config --libs libpjproject)


include ${ROOT}/build-infra/cpp.mk

#${memento-as_test_OBJECT_DIR}/test_interposer.so : ${ROOT}/modules/cpp-common/test_utils/test_interposer.cpp ${ROOT}/modules/cpp-common/test_utils/test_interposer.hpp
#	$(CXX) $(memento-as_test_CPPFLAGS) -shared -fPIC -ldl $< -o $@

include ${ROOT}/modules/cpp-common/makefiles/alarm-utils.mk

${ROOT}/usr/include/memento_as_alarmdefinition.h : ${ROOT}/build/bin/alarm_header ${ROOT}/plugins/memento-as/memento-as.root/usr/share/clearwater/infrastructure/alarms/memento_as_alarms.json
	$< -j "${ROOT}/plugins/memento-as/memento-as.root/usr/share/clearwater/infrastructure/alarms/memento_as_alarms.json" -n "memento_as"
	mv memento_as_alarmdefinition.h $@

CLEANS += ${ROOT}/usr/include/memento_as_alarmdefinition.h

ALARM_DEFINITION_FILES := ${ROOT}/build/memento-as.so/mementoasplugin.o \
                          ${ROOT}/build/memento-as_test/mementoasplugin.o

# Ensure that we generate the alarm definition file before building any of the code that includes it
${ALARM_DEFINITION_FILES} : ${ROOT}/usr/include/memento_as_alarmdefinition.h
