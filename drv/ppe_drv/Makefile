# Makefile for PPE driver

ccflags-y := -I$(obj)/../exports -I$(obj)/..
ccflags-y += -DPPE_DRV_BUILD_ID="$(BUILD_ID)"

obj-m += qca-nss-ppe.o
qca-nss-ppe-objs := ppe_drv.o
qca-nss-ppe-objs += ppe_drv_l3_if.o
qca-nss-ppe-objs += ppe_drv_pppoe.o
qca-nss-ppe-objs += ppe_drv_pub_ip.o
qca-nss-ppe-objs += ppe_drv_vsi.o

ccflags-y += -Wall -Werror
ccflags-y += -DPPE_DRV_DEBUG_LEVEL=0
