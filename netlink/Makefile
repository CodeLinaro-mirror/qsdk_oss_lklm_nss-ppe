ccflags-y := -Werror
ccflags-y += -I$(obj)/include
ccflags-y += -I$(obj)/../exports
ccflags-y += -DNSS_NL_DEBUG_LEVEL=4
ccflags-y += -DNSS_CLIENT_BUILD_ID="$(BUILD_ID)"

ccflags-y += -DCONFIG_NSS_NLIPV4=1
ccflags-y += -DCONFIG_NSS_NLIPV6=1
ccflags-y += -DCONFIG_NSS_NLIPSEC=1
ccflags-y += -DCONFIG_NSS_NLOAM=1
ccflags-y += -DCONFIG_NSS_NLGRE_REDIR_FAMILY=1

qca-nss-netlink-objs := nss_nl.o
qca-nss-netlink-objs += nss_nlgre_redir_family.o
qca-nss-netlink-objs += nss_nlgre_redir_cmd.o
qca-nss-netlink-objs += nss_nlgre_redir_cmn.o
qca-nss-netlink-objs += nss_nlgre_redir.o
qca-nss-netlink-objs += nss_nlgre_redir_lag.o
qca-nss-netlink-objs += nss_nlipv4.o
qca-nss-netlink-objs += nss_nlipv6.o
qca-nss-netlink-objs += nss_nlipsec.o
qca-nss-netlink-objs += nss_nloam.o

CAPWAP_ENABLED:=CONFIG_PACKAGE_kmod-qca-nss-drv-capwapmgr=y
CAPWAP_CONFIG:=$(shell  grep $(CAPWAP_ENABLED) $(TOPDIR)/.config)
ifeq ($(CAPWAP_CONFIG),$(CAPWAP_ENABLED))
ccflags-y += -DCONFIG_NSS_NLCAPWAP=1
qca-nss-netlink-objs += nss_nlcapwap.o
else
ccflags-y += -DCONFIG_NSS_NLCAPWAP=0
endif
DTLS_ENABLED:=CONFIG_PACKAGE_kmod-qca-nss-drv-dtlsmgr=y
DTLS_CONFIG:=$(shell  grep $(DTLS_ENABLED) $(TOPDIR)/.config)
ifeq ($(DTLS_CONFIG),$(DTLS_ENABLED))
ccflags-y += -DCONFIG_NSS_NLDTLS=1
qca-nss-netlink-objs += nss_nldtls.o
else
ccflags-y += -DCONFIG_NSS_NLDTLS=0
endif
ifeq ($(SoC),$(filter $(SoC),ipq807x ipq807x_64 ipq60xx ipq60xx_64))
ccflags-y += -DCONFIG_NSS_NLCRYPTOV2=1
qca-nss-netlink-objs += nss_nlcryptov2.o
else
ccflags-y += -DCONFIG_NSS_NLCRYPTO=1
qca-nss-netlink-objs += nss_nlcrypto.o
endif
obj-m += qca-nss-netlink.o
