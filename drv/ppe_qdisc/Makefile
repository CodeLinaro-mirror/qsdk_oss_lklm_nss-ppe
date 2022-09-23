# Makefile for the PPE Qdisc client

ccflags-y += -I$(obj)/../../exports -I$(obj)/../exports

obj-m += qca-nss-ppe-qdisc.o
qca-nss-ppe-qdisc-objs += ppe_qdisc.o \
			ppe_qdisc_port.o \
			ppe_qdisc_res.o

ccflags-y += -DPPE_QDISC_DEBUG_LEVEL=4
ccflags-y += -Wall -Werror
