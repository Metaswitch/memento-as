all: build

ROOT := $(abspath $(shell pwd)/../..)
MK_DIR := ${ROOT}/plugins/memento-as/mk
PREFIX ?= ${ROOT}/usr
INSTALL_DIR ?= ${PREFIX}
MODULE_DIR := ${ROOT}/plugins/memento-as/modules

DEB_COMPONENT := memento-as
DEB_MAJOR_VERSION := 1.0${DEB_VERSION_QUALIFIER}
DEB_NAMES += memento-as memento-as-dbg

INCLUDE_DIR := ${INSTALL_DIR}/include
LIB_DIR := ${INSTALL_DIR}/lib

BUILD_DIR := ${ROOT}/plugins/memento-as/build

SUBMODULES := thrift cassandra

include $(patsubst %, ${MK_DIR}/%.mk, ${SUBMODULES})
include ${MK_DIR}/memento-as.mk

build: ${SUBMODULES} memento-as

test: ${SUBMODULES} memento-as_test

full_test: ${SUBMODULES} memento-as_full_test
clean: $(patsubst %, %_clean, ${SUBMODULES}) memento-as_clean
	rm -rf ${ROOT}/plugins/memento-as/build

include ${ROOT}/build-infra/cw-deb.mk

.PHONY: deb
deb: all deb-only

.PHONY: all build test clean
