# Makefile for the cranipc client

ccflags-y := -I$(obj) -I$(obj)/..
ccflags-y += -DNSS_CLIENT_BUILD_ID="$(BUILD_ID)"
ccflags-y += -Werror

obj-m += qca-nss-cranipc.o
qca-nss-cranipc-objs := nss_cranipc.o
ccflags-y += -DNSS_CRANIPC_DEBUG_LEVEL=3

obj ?= .
