TARGETS := memento-as.so

TEST_TARGETS := memento-as_test

ROOT := $(abspath $(shell pwd)/../..)
BUILD_DIR := ${ROOT}/plugins/memento-as/build

MK_DIR := ${ROOT}/plugins/memento-as/mk
PREFIX ?= ${ROOT}/usr
INSTALL_DIR ?= ${PREFIX}
MODULE_DIR := ${ROOT}/plugins/memento-as/modules

DEB_COMPONENT := memento-as
DEB_MAJOR_VERSION ?= 1.0${DEB_VERSION_QUALIFIER}
DEB_NAMES += memento-as memento-as-dbg

INCLUDE_DIR := ${INSTALL_DIR}/include
LIB_DIR := ${INSTALL_DIR}/lib

SUBMODULES := thrift cassandra

include $(patsubst %, ${MK_DIR}/%.mk, ${SUBMODULES})

MEMENTO_AS_COMMON_SOURCES := call_list_store.cpp \
                             call_list_store_processor.cpp \
                             cassandra_connection_pool.cpp \
                             cassandra_store.cpp \
                             httpnotifier.cpp \
                             mementoappserver.cpp \
                             mementosaslogger.cpp \
                             sproutletappserver.cpp

memento-as.so_SOURCES := ${MEMENTO_AS_COMMON_SOURCES} \
                         mementoasplugin.cpp \

memento-as_test_SOURCES := ${MEMENTO_AS_COMMON_SOURCES} \
                           accesslogger.cpp \
                           accumulator.cpp \
                           acr.cpp \
                           alarm.cpp \
                           a_record_resolver.cpp \
                           base64.cpp \
                           base_communication_monitor.cpp \
                           baseresolver.cpp \
                           call_list_store_test.cpp \
                           call_list_store_processor_test.cpp \
                           communicationmonitor.cpp \
                           connection_tracker.cpp \
                           counter.cpp \
                           custom_headers.cpp \
                           curl_interposer.cpp \
                           dnscachedresolver.cpp \
                           dnsparser.cpp \
                           exception_handler.cpp \
                           fakecurl.cpp \
                           faketransport_tcp.cpp \
                           health_checker.cpp \
                           httpclient.cpp \
                           httpconnection.cpp \
                           http_connection_pool.cpp \
                           httpnotifier_test.cpp \
                           httpstack.cpp \
                           load_monitor.cpp \
                           log.cpp \
                           logger.cpp \
                           mockhttpnotifier.cpp \
                           mock_sas.cpp \
                           mementoappserver_test.cpp \
                           namespace_hop.cpp \
                           pjutils.cpp \
                           pthread_cond_var_helper.cpp \
                           quiescing_manager.cpp \
                           saslogger.cpp \
                           sip_common.cpp \
                           sipresolver.cpp \
                           stack.cpp \
                           statistic.cpp \
                           test_interposer.cpp \
                           test_main.cpp \
                           thread_dispatcher.cpp \
                           unique.cpp \
                           uri_classifier.cpp \
                           utils.cpp \
                           zmq_lvc.cpp

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

VPATH = ${ROOT}/src:${ROOT}/modules/cpp-common/src:${ROOT}/plugins/memento-as/src:${ROOT}/plugins/memento-as/ut:${ROOT}/modules/cpp-common/test_utils:${ROOT}/src/ut:${ROOT}/plugins/memento-as/modules/memento-common/src:${ROOT}/modules/sas-client/source:${ROOT}/plugins/memento-as/modules/memento-common/ut

include ${ROOT}/build-infra/cpp.mk

${memento-as_test_OBJECT_DIR}/test_interposer.so : ${ROOT}/modules/cpp-common/test_utils/test_interposer.cpp ${ROOT}/modules/cpp-common/test_utils/test_interposer.hpp
	$(CXX) $(memento-as_test_CPPFLAGS) -shared -fPIC -ldl $< -o $@

${memento-as_test_OBJECT_DIR}/curl_interposer.so : ${ROOT}/modules/cpp-common/test_utils/curl_interposer.cpp ${ROOT}/modules/cpp-common/test_utils/curl_interposer.hpp ${ROOT}/modules/cpp-common/test_utils/fakecurl.cpp ${ROOT}/modules/cpp-common/test_utils/fakecurl.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(memento-as_test_CPPFLAGS) -shared -fPIC -ldl $< -o $@
CLEANS += ${memento_test_OBJECT_DIR}/curl_interposer.so

include ${ROOT}/modules/cpp-common/makefiles/alarm-utils.mk

${ROOT}/usr/include/memento_as_alarmdefinition.h : ${ROOT}/build/bin/alarm_header ${ROOT}/plugins/memento-as/memento-as.root/usr/share/clearwater/infrastructure/alarms/memento_as_alarms.json
	$< -j "${ROOT}/plugins/memento-as/memento-as.root/usr/share/clearwater/infrastructure/alarms/memento_as_alarms.json" -n "memento_as"
	mv memento_as_alarmdefinition.h $@

CLEANS += ${ROOT}/usr/include/memento_as_alarmdefinition.h

ALARM_DEFINITION_FILES := ${ROOT}/build/memento-as.so/mementoasplugin.o \
                          ${ROOT}/build/memento-as_test/mementoasplugin.o

# Ensure that we generate the alarm definition file before building any of the code that includes it
${ALARM_DEFINITION_FILES} : ${ROOT}/usr/include/memento_as_alarmdefinition.h

build: ${SUBMODULES} memento-as

test: ${SUBMODULES} memento-as_test

clean: $(patsubst %, %_clean, ${SUBMODULES}) memento-as_clean
	rm -rf ${ROOT}/plugins/memento-as/build

include ${ROOT}/build-infra/cw-deb.mk

.PHONY: deb
deb: all deb-only

.PHONY: all build test clean
