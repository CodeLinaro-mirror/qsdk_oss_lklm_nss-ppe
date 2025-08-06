#Makefile for gre client

obj-m += qca-nss-gre-mgr.o
qca-nss-gre-mgr-y := gre_mgr.o
ccflags-y += -I$(obj) -I$(obj)/../include
ccflags-y += -DGRE_MGR_DEBUG_LEVEL=0
ccflags-y += -Wall -Werror

obj ?= .

#Makefile changes for PPP Frontend

qca-nss-gre-mgr-$(GRE_MGR_FE_PPE_ENABLE) += frontends/ppe/ppe_gre_mgr.o
qca-nss-gre-mgr-$(GRE_MGR_FE_PPE_ENABLE) += frontends/ppe/ppe_gre_mgr_stats.o
ccflags-$(GRE_MGR_FE_PPE_ENABLE) += -I$(obj)/frontends/ppe
ccflags-$(GRE_MGR_FE_PPE_ENABLE) += -I$(obj)/../../exports -I$(obj)/../../drv/exports
ccflags-$(GRE_MGR_FE_PPE_ENABLE) += -DGRE_MGR_FE_PPE_ENABLE
