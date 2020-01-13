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
#include <rte_bus_vmbus.h>

#include "json_writer.h"
#include "portinfo.h"

/* multi-packet data from host */
struct hn_rx_bufinfo;

#define	HN_MAX_MAC_ADDRS	1
#define HN_MAX_CHANNELS		64
#define HN_INVALID_PORT	UINT16_MAX

struct hn_data {
	struct rte_vmbus_device *vmbus;
	struct hn_rx_queue *primary;
	rte_spinlock_t  vf_lock;
	uint16_t	port_id;
	uint16_t	vf_port;

	uint8_t		vf_present;
	uint8_t		closed;
	uint8_t		vlan_strip;

	uint32_t	link_status;
	uint32_t	link_speed;

	struct rte_mem_resource *rxbuf_res;	/* UIO resource for Rx */
	struct hn_rx_bufinfo *rxbuf_info;
	uint32_t	rxbuf_section_cnt;	/* # of Rx sections */
	volatile uint32_t rxbuf_outstanding;
	uint16_t	max_queues;		/* Max available queues */
	uint16_t	num_queues;
	uint64_t	rss_offloads;

	struct rte_mem_resource *chim_res;	/* UIO resource for Tx */
	struct rte_mempool *tx_pool;		/* Tx descriptors */
	uint32_t	chim_szmax;		/* Max size per buffer */
	uint32_t	chim_cnt;		/* Max packets per buffer */

	uint32_t	latency;
	uint32_t	nvs_ver;
	uint32_t	ndis_ver;
	uint32_t	rndis_agg_size;
	uint32_t	rndis_agg_pkts;
	uint32_t	rndis_agg_align;

	volatile uint32_t  rndis_pending;
	rte_atomic32_t	rndis_req_id;
	uint8_t		rndis_resp[256];

	uint32_t	rss_hash;
	uint8_t		rss_key[40];
	uint16_t	rss_ind[128];

	struct rte_ether_addr mac_addr;

	struct rte_eth_dev_owner owner;
	struct rte_intr_handle vf_intr;

	struct vmbus_channel *channels[HN_MAX_CHANNELS];
};

struct vmbus_br {
	struct vmbus_bufring *vbr;
	uint32_t	dsize;
	uint32_t	windex; /* next available location */
};

struct vmbus_channel {
	STAILQ_HEAD(, vmbus_channel) subchannel_list;
	STAILQ_ENTRY(vmbus_channel) next;
	const struct rte_vmbus_device *device;

	struct vmbus_br rxbr;
	struct vmbus_br txbr;

	uint16_t relid;
	uint16_t subchannel_id;
	uint8_t monitor_id;
};

/* Amount of space available for write */
static inline uint32_t
vmbus_br_availwrite(const struct vmbus_br *br, uint32_t windex)
{
	uint32_t rindex = br->vbr->rindex;

	if (windex >= rindex)
		return br->dsize - (windex - rindex);
	else
		return rindex - windex;
}

static inline uint32_t
vmbus_br_availread(const struct vmbus_br *br)
{
	return br->dsize - vmbus_br_availwrite(br, br->vbr->windex);
}

static void vmbus_dump_ring(json_writer_t *j, const char *name,
			    const struct vmbus_br *br)
{
	const struct vmbus_bufring *vbr = br->vbr;

	jsonw_name(j, name);
	jsonw_start_object(j);

	jsonw_uint_field(j, "write_index", vbr->windex);
	jsonw_uint_field(j, "read_index", vbr->rindex);
	jsonw_bool_field(j, "interrupt_mask", vbr->imask);

	if (vbr->feature_bits.feat_pending_send_sz)
		jsonw_uint_field(j, "pending_send", vbr->pending_send);

	jsonw_uint_field(j, "size", br->dsize);
	jsonw_uint_field(j, "avail_write",
			 vmbus_br_availwrite(br, vbr->windex));
	jsonw_uint_field(j, "avail_read", vmbus_br_availread(br));

	jsonw_end_object(j);
}

static void vmbus_chan_dump(json_writer_t *j,
			    const struct vmbus_channel *chan)
{
	jsonw_start_object(j);
	jsonw_uint_field(j, "idl", chan->subchannel_id);
	jsonw_uint_field(j, "relid", chan->relid);
	jsonw_uint_field(j, "monitor_id", chan->monitor_id);

	vmbus_dump_ring(j, "rxbr", &chan->rxbr);
	vmbus_dump_ring(j, "txbr", &chan->txbr);
	jsonw_end_object(j);
}

void netvsc_show(json_writer_t *j, uint16_t port_id)
{
	struct rte_eth_dev *eth_dev = &rte_eth_devices[port_id];
	struct hn_data *hv = eth_dev->data->dev_private;
	unsigned int i;

	jsonw_uint_field(j, "max_queues", hv->max_queues);
	jsonw_uint_field(j, "num_queues", hv->num_queues);

	jsonw_name(j, "channels");
	jsonw_start_array(j);
	for (i = 0; i < hv->num_queues; i++) {
		vmbus_chan_dump(j, hv->channels[i]);
	}
	jsonw_end_array(j);

	if (hv->vf_present) {
		jsonw_name(j, "vf");
		jsonw_start_object(j);
		show_port(j, hv->vf_port);
		jsonw_end_object(j);
	}
}
