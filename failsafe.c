/* Look inside failsafe */
/* Deep dive inside driver */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include <rte_eal.h>
#include <rte_tailq.h>
#include <rte_ethdev.h>
#include <rte_atomic.h>
#include <rte_mempool.h>

#include "json_writer.h"
#include "portinfo.h"

#define FAILSAFE_MAX_ETHPORTS 2
#define FAILSAFE_MAX_ETHADDR 128


enum dev_state {
	DEV_UNDEFINED,
	DEV_PARSED,
	DEV_PROBED,
	DEV_ACTIVE,
	DEV_STARTED,
};

struct fs_stats {
	struct rte_eth_stats stats;
	uint64_t timestamp;
};

enum rxp_service_state {
	SS_NO_SERVICE = 0,
	SS_REGISTERED,
	SS_READY,
	SS_RUNNING,
};

struct rx_proxy {
	/* epoll file descriptor */
	int efd;
	/* event vector to be used by epoll */
	struct rte_epoll_event *evec;
	/* rte service id */
	uint32_t sid;
	/* service core id */
	uint32_t scid;
	enum rxp_service_state sstate;
};

/*
 * This is referenced by eth_dev->data->dev_private
 * This is shared between processes.
 */
struct fs_priv {
	struct rte_eth_dev_data *data; /* backreference to shared data. */
	/*
	 * Set of sub_devices.
	 * subs[0] is the preferred device
	 * any other is just another slave
	 */
	struct sub_device *subs;  /* shared between processes */
	uint8_t subs_head; /* if head == tail, no subs */
	uint8_t subs_tail; /* first invalid */
	uint8_t subs_tx; /* current emitting device */
	uint8_t current_probed;
	/* flow mapping */
	TAILQ_HEAD(sub_flows, rte_flow) flow_list;
	/* current number of mac_addr slots allocated. */
	uint32_t nb_mac_addr;
	struct rte_ether_addr mac_addrs[FAILSAFE_MAX_ETHADDR];
	uint32_t mac_addr_pool[FAILSAFE_MAX_ETHADDR];
	uint32_t nb_mcast_addr;
	struct rte_ether_addr *mcast_addrs;
	/* current capabilities */
	struct rte_eth_dev_owner my_owner; /* Unique owner. */
	struct rte_intr_handle intr_handle; /* Port interrupt handle. */
	/*
	 * Fail-safe state machine.
	 * This level will be tracking state of the EAL and eth
	 * layer at large as defined by the user application.
	 * It will then steer the sub_devices toward the same
	 * synchronized state.
	 */
	enum dev_state state;
	struct rte_eth_stats stats_accumulator;
	/*
	 * Rx interrupts/events proxy.
	 * The PMD issues Rx events to the EAL on behalf of its subdevices,
	 * it does that by registering an event-fd for each of its queues with
	 * the EAL. A PMD service thread listens to all the Rx events from the
	 * subdevices, when an Rx event is issued by a subdevice it will be
	 * caught by this service with will trigger an Rx event in the
	 * appropriate failsafe Rx queue.
	 */
	struct rx_proxy rxp;
	pthread_mutex_t hotplug_mutex;
	/* Hot-plug mutex is locked by the alarm mechanism. */
	volatile unsigned int alarm_lock:1;
	unsigned int pending_alarm:1; /* An alarm is pending */
	/* flow isolation state */
	int flow_isolated:1;
};

struct sub_device {
	/* Exhaustive DPDK device description */
	struct sub_device *next;
	struct rte_devargs devargs;
	struct rte_bus *bus; /* for primary process only. */
	struct rte_device *dev; /* for primary process only. */
	uint8_t sid;
	/* Device state machine */
	enum dev_state state;
	/* Last stats snapshot passed to user */
	struct fs_stats stats_snapshot;
	/* Some device are defined as a command line */
	char *cmdline;
	/* Others are retrieved through a file descriptor */
	char *fd_str;
	/* fail-safe device backreference */
	uint16_t fs_port_id; /* shared between processes */
	/* sub device port id*/
	uint16_t sdev_port_id; /* shared between processes */
	/* flag calling for recollection */
	volatile unsigned int remove:1;
	/* flow isolation state */
	int flow_isolated:1;
	/* RMV callback registration state */
	unsigned int rmv_callback:1;
	/* LSC callback registration state */
	unsigned int lsc_callback:1;
};

static const char *dev_state[] = {
	[DEV_UNDEFINED] = "UNDEFINED",
	[DEV_PARSED] = "PARSED",
	[DEV_PROBED] = "PROBED",
	[DEV_ACTIVE] = "ACTIVE",
	[DEV_STARTED] = "STARTED",
};

void failsafe_show(json_writer_t *j, uint16_t port_id)
{
	struct rte_eth_dev *dev = &rte_eth_devices[port_id];
	struct fs_priv *fs = dev->data->dev_private;
	uint8_t sid;

	jsonw_string_field(j, "state", dev_state[fs->state]);

	jsonw_name(j, "subs");
	jsonw_start_array(j);
	for (sid = 0; sid < fs->subs_tail; ++sid) {
		struct sub_device *subs = fs->subs + sid;

		if (subs->state == DEV_UNDEFINED)
			continue;

		jsonw_start_object(j);
		jsonw_string_field(j, "state", dev_state[subs->state]);
		show_port(j, subs->sdev_port_id);
		jsonw_end_object(j);

	}
	jsonw_end_array(j);
}
