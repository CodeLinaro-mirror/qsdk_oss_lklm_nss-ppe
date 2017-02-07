# Makefile for the ipsecmgr client

ccflags-y := -I$(obj) -I$(obj)/..
ccflags-y += -DNSS_CLIENTS_BUILD_ID=\"'Build_ID - $(shell date +'%m/%d/%y, %H:%M:%S')'\"
ifeq ($(SoC), fsm9010)
	ccflags-y += -DNSS_IPSECMGR_PMTU_SUPPORT
endif

obj-m += qca-nss-ipsecmgr.o
qca-nss-ipsecmgr-objs := nss_ipsecmgr.o
qca-nss-ipsecmgr-objs += nss_ipsecmgr_flow.o
qca-nss-ipsecmgr-objs += nss_ipsecmgr_sa.o
qca-nss-ipsecmgr-objs += nss_ipsecmgr_subnet.o
ccflags-y += -DNSS_IPSECMGR_DEBUG_LEVEL=3

obj ?= .
