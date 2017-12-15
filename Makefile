TARGETS := memento-as.so

TEST_TARGETS := memento-as_test

ROOT := $(abspath $(shell pwd)/../..)
MK_DIR := ${ROOT}/plugins/memento-as/mk
PREFIX ?= ${ROOT}/usr
INSTALL_DIR ?= ${PREFIX}
MODULE_DIR := ${ROOT}/plugins/memento-as/modules

INCLUDE_DIR := ${INSTALL_DIR}/include
LIB_DIR := ${INSTALL_DIR}/lib
BUILD_DIR := ${ROOT}/plugins/memento-as/build

MEMENTO_AS_COMMON_SOURCES := mementoasplugin.cpp \
                             call_list_store.cpp \
                             mementosaslogger.cpp \
                             call_list_store_processor.cpp \
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
                             mementoappserver.cpp

memento-as.so_SOURCES := ${MEMENTO_AS_COMMON_SOURCES}

memento-as_test_SOURCES := ${MEMENTO_AS_COMMON_SOURCES}

COMMON_CPPFLAGS := -I${ROOT}/include \
                   -I${ROOT}/usr/include \
                   -I${ROOT}/modules/cpp-common/include \
                   -I${ROOT}/modules/sas-client/include \
                   -I${ROOT}/modules/app-servers/include \
                   -I${ROOT}/modules/rapidjson/include \
                   -I${ROOT}/plugins/memento-as/modules/memento-common/include \
                   -I${ROOT}/plugins/memento-as/include \
                   -Wno-write-strings \
                   `PKG_CONFIG_PATH=${ROOT}/usr/lib/pkgconfig pkg-config --cflags libpjproject` \
                   -fPIC

memento-as.so_CPPFLAGS := ${COMMON_CPPFLAGS}
memento-as_test_CPPFLAGS := ${COMMON_CPPFLAGS} -DGTEST_USE_OWN_TR1_TUPLE = 0 -Wno-write-strings

COMMON_LDFLAGS := ${COMMON_LDFLAGS} \
                  -L${ROOT}/usr/lib \
                  -levent \
                  -levhtp \
                  -lsas \
                  -lcares \
                  -levent_pthreads \
                  -lboost_regex \
                  -lpthread \
                  -lthrift \
                  -ldl \
                  -lboost_system \
                  -lcurl \
                  -lzmq \
                  -lcassandra \
                  -lz

memento-as.so_LDFLAGS := -shared ${COMMON_LDFLAGS}
memento-as_test_LDFLAGS := ${COMMON_LDFLAGS}

SUBMODULES := thrift cassandra

include ${ROOT}/build-infra/cpp.mk
include $(patsubst %, ${MK_DIR}/%.mk, ${SUBMODULES})

VPATH = ${ROOT}/src:${ROOT}/modules/cpp-common/src:${ROOT}/plugins/memento-as/src:${ROOT}/plugins/memento-as/ut:${ROOT}/modules/cpp-common/test_utils:${ROOT}/src/ut:${ROOT}/plugins/memento-as/modules/memento-common/src:${ROOT}/modules/sas-client/source

${BUILD_DIR}/bin/memento-as_test : ${memento-as_test_OBJECT_DIR}/md5.o

${memento-as_test_OBJECT_DIR}/test_interposer.so : ../modules/cpp-common/test_utils/test_interposer.cpp ../modules/cpp-common/test_utils/test_interposer.hpp
	$(CXX) $(memento-as_test_CPPFLAGS) -shared -fPIC -ldl $< -o $@

DEB_COMPONENT := memento-as
DEB_MAJOR_VERSION := 1.0${DEB_VERSION_QUALIFIER}
DEB_NAMES += memento-as memento-as-dbg

include ${ROOT}/build-infra/cw-deb.mk

.PHONY: deb
deb: all deb-only

include ${ROOT}/modules/cpp-common/makefiles/alarm-utils.mk

${ROOT}/usr/include/memento_as_alarmdefinition.h : ${ROOT}/build/bin/alarm_header ${ROOT}/plugins/memento-as/memento-as.root/usr/share/clearwater/infrastructure/alarms/memento_as_alarms.json
	$< -j "memento-as.root/usr/share/clearwater/infrastructure/alarms/memento_as_alarms.json" -n "memento_as"
	mv memento_as_alarmdefinition.h $@
${memento-as.so_OBJECT_DIR}/mementoasplugin.o : ../usr/include/memento_as_alarmdefinition.h
CLEANS += ${ROOT}/usr/include/memento_as_alarmdefinition.h

# Ensure that we generate the alarm definition file before building any of the code that includes it
${ROOT}/plugins/memento-as/src/$(MEMENTO_AS_COMMON_SOURCES): ${ROOT}/usr/include/memento_as_alarmdefinition.h
