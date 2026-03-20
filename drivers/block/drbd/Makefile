# SPDX-License-Identifier: GPL-2.0-only
drbd-y := drbd_buildtag.o drbd_bitmap.o drbd_proc.o
drbd-y += drbd_sender.o drbd_receiver.o drbd_req.o drbd_actlog.o
drbd-y += drbd_main.o drbd_strings.o drbd_nl.o
drbd-y += drbd_interval.o drbd_state.o
drbd-y += drbd_nla.o
drbd-y += drbd_transport.o
drbd-$(CONFIG_DEV_DAX_PMEM) += drbd_dax_pmem.o
drbd-$(CONFIG_DEBUG_FS) += drbd_debugfs.o
drbd-$(CONFIG_DRBD_COMPAT_84) += drbd_legacy_84.o

obj-$(CONFIG_BLK_DEV_DRBD)     += drbd.o

obj-$(CONFIG_BLK_DEV_DRBD_TCP) += drbd_transport_tcp.o
obj-$(CONFIG_BLK_DEV_DRBD_LB_TCP) += drbd_transport_lb-tcp.o
obj-$(CONFIG_BLK_DEV_DRBD_RDMA) += drbd_transport_rdma.o
