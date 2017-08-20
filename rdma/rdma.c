/*
 * rdma.c	RDMA tool
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Authors:     Leon Romanovsky <leonro@mellanox.com>
 */

#include "rdma.h"
#include "SNAPSHOT.h"

static void help(char *name)
{
	pr_out("Usage: %s [ OPTIONS ] OBJECT { COMMAND | help }\n"
	       "where  OBJECT := { dev | help }\n"
	       "       OPTIONS := { -V[ersion] | -d[etails]}\n", name);
}

static int cmd_help(struct rd *rd)
{
	help(rd->filename);
	return 0;
}

static int rd_cmd(struct rd *rd)
{
	const struct rd_cmd cmds[] = {
		{ NULL,		cmd_help },
		{ "help",	cmd_help },
		{ "dev",	cmd_dev },
		{ 0 }
	};

	return rd_exec_cmd(rd, cmds, "object");
}

static int rd_init(struct rd *rd, int argc, char **argv, char *filename)
{
	uint32_t seq;
	int ret;

	rd->filename = filename;
	rd->argc = argc;
	rd->argv = argv;
	INIT_LIST_HEAD(&rd->dev_map_list);
	rd->buff = malloc(MNL_SOCKET_BUFFER_SIZE);
	if (!rd->buff)
		return -ENOMEM;

	rd_prepare_msg(rd, RDMA_NLDEV_CMD_GET,
		       &seq, (NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP));
	ret = rd_send_msg(rd);
	if (ret)
		return ret;

	return rd_recv_msg(rd, rd_dev_init_cb, rd, seq);
}

static void rd_free(struct rd *rd)
{
	free(rd->buff);
	rd_free_devmap(rd);
}

int main(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "version",		no_argument,		NULL, 'V' },
		{ "help",		no_argument,		NULL, 'h' },
		{ "details",		no_argument,		NULL, 'd' },
		{ NULL, 0, NULL, 0 }
	};
	bool show_details = false;
	char *filename;
	struct rd rd;
	int opt;
	int err;

	filename = basename(argv[0]);

	while ((opt = getopt_long(argc, argv, "Vhd",
				  long_options, NULL)) >= 0) {
		switch (opt) {
		case 'V':
			printf("%s utility, iproute2-ss%s\n",
			       filename, SNAPSHOT);
			return EXIT_SUCCESS;
		case 'd':
			show_details = true;
			break;
		case 'h':
			help(filename);
			return EXIT_SUCCESS;
		default:
			pr_err("Unknown option.\n");
			help(filename);
			return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv += optind;

	err = rd_init(&rd, argc, argv, filename);
	if (err)
		goto out;

	rd.show_details = show_details;
	err = rd_cmd(&rd);
out:
	/* Always cleanup */
	rd_free(&rd);
	return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
