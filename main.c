/*
 * Secondary process to get stats from netvsc device.
 */

#include <stdio.h>
#include <err.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <net/if.h>

#include <rte_eal.h>
#include <rte_ethdev.h>

#include "json_writer.h"
#include "portinfo.h"

static void show_port_stats(json_writer_t *j, uint16_t port_id)
{
	struct rte_eth_stats stats;

	if (rte_eth_stats_get(port_id, &stats) != 0)
		return;

	jsonw_u64_field(j, "ibytes", stats.ibytes);
	jsonw_u64_field(j, "ipackets", stats.ipackets);
	jsonw_u64_field(j, "imissed", stats.imissed);
	jsonw_u64_field(j, "ierrors", stats.ierrors);

	jsonw_u64_field(j, "obytes", stats.obytes);
	jsonw_u64_field(j, "opackets", stats.opackets);
	jsonw_u64_field(j, "oerrors", stats.oerrors);
}

static void show_port_xstats(json_writer_t *j, uint16_t port_id)
{
	struct rte_eth_xstat_name *names;
	struct rte_eth_xstat *stats;
	unsigned int i, len;
	int r;

	r = rte_eth_xstats_get_names(port_id, NULL, 0);
	if (r < 0)
		return;

	len = (unsigned int)r;
	names = alloca(sizeof(*names) * len);
	stats = alloca(sizeof(*stats) * len);
	if (!names || !stats)
		errx(1, "out of memory\n");

	r = rte_eth_xstats_get_names(port_id, names, len);
	if (r < 0)
		errx(1, "xstats_get_names failed: %s\n", strerror(-r));

	r = rte_eth_xstats_get(port_id, stats, len);
	if (r < 0)
		errx(1, "xstats_get failed: %s\n", strerror(-r));

	jsonw_name(j, "xstats");
	jsonw_start_object(j);
	for (i = 0; i < len; i++) {
		uint64_t v = stats[i].value;

		if (v != 0)
			jsonw_u64_field(j, names[i].name, v);
	}
	jsonw_end_object(j);
}

static void show_port_info(json_writer_t *j,
			   const struct rte_eth_dev_info *info)
{
	char *ifname, ifbuf[IFNAMSIZ];

	jsonw_string_field(j, "driver", info->driver_name);

	if (info->device)
		jsonw_string_field(j, "device", info->device->name);

	if (info->if_index != 0 &&
	    (ifname = if_indextoname(info->if_index, ifbuf)))
		jsonw_string_field(j, "interface", ifname);
}

static void show_port_mac(json_writer_t *j,
			  const struct rte_ether_addr *mac)
{
	char ebuf[RTE_ETHER_ADDR_FMT_SIZE];

	rte_ether_format_addr(ebuf, sizeof(ebuf), mac);
	jsonw_string_field(j, "mac", ebuf);
}

void show_port(json_writer_t *j, uint16_t port_id)
{
	struct rte_eth_dev_info info;
	struct rte_ether_addr mac;
	int r;

	r = rte_eth_dev_info_get(port_id, &info);
	if (r < 0)
		errx(1, "dev_info_get failed: %s\n", strerror(-r));

	jsonw_uint_field(j, "port", port_id);
	show_port_info(j, &info);

	r = rte_eth_macaddr_get(port_id, &mac);
	if (r < 0)
		errx(1, "macaddr_get failed\n");

	show_port_mac(j, &mac);

	show_port_stats(j, port_id);
	show_port_xstats(j, port_id);

	if (strcmp(info.driver_name, "net_netvsc") == 0)
		netvsc_show(j, port_id);
	else if (strcmp(info.driver_name, "net_failsafe") == 0)
		failsafe_show(j, port_id);
}

int main(int argc, char **argv)
{
	json_writer_t *j;
	uint16_t port_id;
	int r;

	rte_log_set_global_level(RTE_LOG_ERR);
	r = rte_eal_init(argc, argv);
	if (r < 0)
		errx(1, "cannot init EAL\n");

	j = jsonw_new(stdout);
	if (j == NULL)
		err(1, "jsonw");
	jsonw_pretty(j, true);

	jsonw_start_array(j);
	RTE_ETH_FOREACH_DEV(port_id) {
		jsonw_start_object(j);
		show_port(j, port_id);
		jsonw_end_object(j);
	}
	jsonw_end_array(j);

	jsonw_destroy(&j);

	rte_eal_cleanup();
	return 0;
}
