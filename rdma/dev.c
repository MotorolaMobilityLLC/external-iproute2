/*
 * dev.c	RDMA tool
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Authors:     Leon Romanovsky <leonro@mellanox.com>
 */

#include "rdma.h"

static int dev_help(struct rd *rd)
{
	pr_out("Usage: %s dev show [DEV]\n", rd->filename);
	return 0;
}

static const char *dev_caps_to_str(uint32_t idx)
{
#define RDMA_DEV_FLAGS(x) \
	x(RESIZE_MAX_WR, 0) \
	x(BAD_PKEY_CNTR, 1) \
	x(BAD_QKEY_CNTR, 2) \
	x(RAW_MULTI, 3) \
	x(AUTO_PATH_MIG, 4) \
	x(CHANGE_PHY_PORT, 5) \
	x(UD_AV_PORT_ENFORCE_PORT_ENFORCE, 6) \
	x(CURR_QP_STATE_MOD, 7) \
	x(SHUTDOWN_PORT, 8) \
	x(INIT_TYPE, 9) \
	x(PORT_ACTIVE_EVENT, 10) \
	x(SYS_IMAGE_GUID, 11) \
	x(RC_RNR_NAK_GEN, 12) \
	x(SRQ_RESIZE, 13) \
	x(N_NOTIFY_CQ, 14) \
	x(LOCAL_DMA_LKEY, 15) \
	x(MEM_WINDOW, 17) \
	x(UD_IP_CSUM, 18) \
	x(UD_TSO, 19) \
	x(XRC, 20) \
	x(MEM_MGT_EXTENSIONS, 21) \
	x(BLOCK_MULTICAST_LOOPBACK, 22) \
	x(MEM_WINDOW_TYPE_2A, 23) \
	x(MEM_WINDOW_TYPE_2B, 24) \
	x(RC_IP_CSUM, 25) \
	x(RAW_IP_CSUM, 26) \
	x(CROSS_CHANNEL, 27) \
	x(MANAGED_FLOW_STEERING, 29) \
	x(SIGNATURE_HANDOVER, 30) \
	x(ON_DEMAND_PAGING, 31) \
	x(SG_GAPS_REG, 32) \
	x(VIRTUAL_FUNCTION, 33) \
	x(RAW_SCATTER_FCS, 34) \
	x(RDMA_NETDEV_OPA_VNIC, 35)

	enum { RDMA_DEV_FLAGS(RDMA_BITMAP_ENUM) };

	static const char * const
		rdma_dev_names[] = { RDMA_DEV_FLAGS(RDMA_BITMAP_NAMES) };
	#undef RDMA_DEV_FLAGS

	if (idx < ARRAY_SIZE(rdma_dev_names) && rdma_dev_names[idx])
		return rdma_dev_names[idx];
	return "UNKNOWN";
}

static void dev_print_caps(struct nlattr **tb)
{
	uint64_t caps;
	uint32_t idx;

	if (!tb[RDMA_NLDEV_ATTR_CAP_FLAGS])
		return;

	caps = mnl_attr_get_u64(tb[RDMA_NLDEV_ATTR_CAP_FLAGS]);

	pr_out("\n    caps: <");
	for (idx = 0; caps; idx++) {
		if (caps & 0x1) {
			pr_out("%s", dev_caps_to_str(idx));
			if (caps >> 0x1)
				pr_out(", ");
		}
		caps >>= 0x1;
	}

	pr_out(">");
}

static void dev_print_fw(struct nlattr **tb)
{
	if (!tb[RDMA_NLDEV_ATTR_FW_VERSION])
		return;

	pr_out("fw %s ",
	       mnl_attr_get_str(tb[RDMA_NLDEV_ATTR_FW_VERSION]));
}

static void dev_print_node_guid(struct nlattr **tb)
{
	uint64_t node_guid;

	if (!tb[RDMA_NLDEV_ATTR_NODE_GUID])
		return;

	node_guid = mnl_attr_get_u64(tb[RDMA_NLDEV_ATTR_NODE_GUID]);
	rd_print_u64("node_guid", node_guid);
}

static void dev_print_sys_image_guid(struct nlattr **tb)
{
	uint64_t sys_image_guid;

	if (!tb[RDMA_NLDEV_ATTR_SYS_IMAGE_GUID])
		return;

	sys_image_guid = mnl_attr_get_u64(tb[RDMA_NLDEV_ATTR_SYS_IMAGE_GUID]);
	rd_print_u64("sys_image_guid", sys_image_guid);
}

static const char *node_type_to_str(uint8_t node_type)
{
	static const char * const node_type_str[] = { "unknown", "ca",
						      "switch", "router",
						      "rnic", "usnic",
						      "usnic_dp" };
	if (node_type < ARRAY_SIZE(node_type_str))
		return node_type_str[node_type];
	return "unknown";
}

static void dev_print_node_type(struct nlattr **tb)
{
	uint8_t node_type;

	if (!tb[RDMA_NLDEV_ATTR_DEV_NODE_TYPE])
		return;

	node_type = mnl_attr_get_u8(tb[RDMA_NLDEV_ATTR_DEV_NODE_TYPE]);
	pr_out("node_type %s ", node_type_to_str(node_type));
}

static int dev_parse_cb(const struct nlmsghdr *nlh, void *data)
{
	struct nlattr *tb[RDMA_NLDEV_ATTR_MAX] = {};
	struct rd *rd = data;

	mnl_attr_parse(nlh, 0, rd_attr_cb, tb);
	if (!tb[RDMA_NLDEV_ATTR_DEV_INDEX] || !tb[RDMA_NLDEV_ATTR_DEV_NAME])
		return MNL_CB_ERROR;

	pr_out("%u: %s: ",
	       mnl_attr_get_u32(tb[RDMA_NLDEV_ATTR_DEV_INDEX]),
	       mnl_attr_get_str(tb[RDMA_NLDEV_ATTR_DEV_NAME]));
	dev_print_node_type(tb);
	dev_print_fw(tb);
	dev_print_node_guid(tb);
	dev_print_sys_image_guid(tb);
	if (rd->show_details)
		dev_print_caps(tb);

	pr_out("\n");
	return MNL_CB_OK;
}

static int dev_no_args(struct rd *rd)
{
	uint32_t seq;
	int ret;

	rd_prepare_msg(rd, RDMA_NLDEV_CMD_GET,
		       &seq, (NLM_F_REQUEST | NLM_F_ACK));
	mnl_attr_put_u32(rd->nlh, RDMA_NLDEV_ATTR_DEV_INDEX, rd->dev_idx);
	ret = rd_send_msg(rd);
	if (ret)
		return ret;

	return rd_recv_msg(rd, dev_parse_cb, rd, seq);
}

static int dev_one_show(struct rd *rd)
{
	const struct rd_cmd cmds[] = {
		{ NULL,		dev_no_args},
		{ 0 }
	};

	return rd_exec_cmd(rd, cmds, "parameter");
}

static int dev_show(struct rd *rd)
{
	struct dev_map *dev_map;
	int ret = 0;

	if (rd_no_arg(rd)) {
		list_for_each_entry(dev_map, &rd->dev_map_list, list) {
			rd->dev_idx = dev_map->idx;
			ret = dev_one_show(rd);
			if (ret)
				return ret;
		}

	} else {
		dev_map = dev_map_lookup(rd, false);
		if (!dev_map) {
			pr_err("Wrong device name\n");
			return -ENOENT;
		}
		rd_arg_inc(rd);
		rd->dev_idx = dev_map->idx;
		ret = dev_one_show(rd);
	}
	return ret;
}

int cmd_dev(struct rd *rd)
{
	const struct rd_cmd cmds[] = {
		{ NULL,		dev_show },
		{ "show",	dev_show },
		{ "list",	dev_show },
		{ "help",	dev_help },
		{ 0 }
	};

	return rd_exec_cmd(rd, cmds, "dev command");
}
