# Makefile for the PPE driver

ccflags-y := -I$(obj) -I$(obj)/.. -I$(obj)/../..

obj-$(bridge-mgr)+= bridge/
obj-$(vlan-mgr)+= vlan/
obj-$(pppoe-mgr)+= pppoe/
obj-$(lag-mgr)+= lag/

obj ?= .
