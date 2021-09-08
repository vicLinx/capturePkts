#include "main.h"
#include <rte_ip.h>

/* l2fwd loop */
static void
l2fwd_loop(void)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	struct rte_mbuf *pkts_clone[MAX_PKT_BURST];
	unsigned lcore_id;
	unsigned i, j, portid, nb_rx, dst_port;
	struct lcore_queue_conf *qconf;

	lcore_id = rte_lcore_id();
	qconf = &lcore_queue_conf[lcore_id];

	if (qconf->n_rx_port == 0) {
		RTE_LOG(INFO, CAPTURE, "lcore %u has nothing to do\n", lcore_id);
		return;
	}

	RTE_LOG(INFO, CAPTURE, "entering main loop on lcore %u\n", lcore_id);

	for (i = 0; i < qconf->n_rx_port; i++) {
		portid = qconf->rx_port_list[i];
		RTE_LOG(INFO, CAPTURE, " -- lcoreid=%u portid=%u\n", lcore_id,
			portid);

	}

	while (!force_quit) {

		for (i = 0; i < qconf->n_rx_port; i++) {
			
			// 接收
			portid = qconf->rx_port_list[i];
			nb_rx = rte_eth_rx_burst((uint8_t) portid, 0, pkts_burst, MAX_PKT_BURST);

			// Linx: 增加 mbufs 的备份，并将其放入 ring_clone
			for (j = 0; j < nb_rx; j++) {
				rte_prefetch0(rte_pktmbuf_mtod(pkts_burst[j], void *));
				pkts_clone[j] = rte_pktmbuf_clone(pkts_burst[j], pool_clone);
			}
			rte_ring_mp_enqueue_bulk(ring_clone, (void *)pkts_clone, nb_rx, NULL);

			// 发送
			dst_port = capture_dst_ports[portid];
			rte_eth_tx_burst((uint8_t) dst_port, 0, pkts_burst, nb_rx);
		}
	}
}

static void
capture_loop(void) 		// Linx
{							

	unsigned num_pkts, ret_deq, i;
	struct rte_mbuf *pkts_free[NB_MBUF];

	while (!force_quit) {

		num_pkts = rte_ring_count(ring_clone);
		if (num_pkts > 2048){

			ret_deq = rte_ring_mc_dequeue_bulk(ring_clone, (void **)pkts_free,
							num_pkts, NULL);				
			
			if (ret_deq > 0) {
				
				// Linx: 解析出所需部分，将其入环，再将 clone_mbuf 释放
				//       这里 以 ipv4 header 为例
				struct ipv4_hdr *iph, *tmp;
				for(i=0; i<ret_deq; i++) {
					
					/* ==== 具体抓包 ==== */
					iph = (struct ipv4_hdr *) malloc (sizeof(struct ipv4_hdr)); 
					tmp = rte_pktmbuf_mtod_offset(
                				pkts_free[i], 
                				struct ipv4_hdr *, 
                				sizeof(struct ether_hdr)
            				);

					iph->packet_id = tmp->packet_id;
					iph->dst_addr = tmp->dst_addr;
					iph->src_addr = tmp->src_addr;
					iph->total_length = tmp->total_length;
					iph->src_addr = tmp->src_addr;
					iph->type_of_service = tmp->type_of_service;
					iph->hdr_checksum = tmp->hdr_checksum;
					iph->fragment_offset = tmp->fragment_offset;
					iph->next_proto_id = tmp->next_proto_id;
					iph->version_ihl = tmp->version_ihl;

					rte_ring_mp_enqueue(ring_res, iph);
					rte_pktmbuf_free(pkts_free[i]);

				}
			}
		}
	}
}

static void
output_loop(void)
{

	while (!force_quit){
		
		struct ipv4_hdr *iph;
		if (rte_ring_sc_dequeue(ring_res, (void **)&iph) == 0) {
			// Linx: 这里只输出 ipv4 包头的 packet_id 字段
			RTE_LOG(INFO, CAPTURE, " This packet id is %hu\n ", iph->packet_id);
			free(iph);
		}

	
	}

}

static int
lcores_main_loop(__attribute__((unused)) void *arg) 	// Linx 
{	
	unsigned lcore_id;

	lcore_id = rte_lcore_id();
	if (lcore_id == 0) {		// Linx: lcore 0 默认用来打印输出报文信息
		capture_loop();
		return 0;
	}
	if (lcore_id == 1) {
		output_loop();
		return 0;
	}
	else {
		l2fwd_loop();
		return 0;
	}

	return 0;
}

int
main(int argc, char **argv)
{
	int ret;
	unsigned lcore_id;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
	argc -= ret;
	argv += ret;

	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* parse application arguments (after the EAL ones) */
	ret = capture_parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid CAPTURE arguments\n");

	/* Linx: 初始化相关配置*/
	init_mbuf_pools();
	init_ring();
	init_ports();
	output_file = fopen("./output", "w");
	rte_openlog_stream(output_file);

	ret = 0;
	/* launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(lcores_main_loop, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0) {
			ret = -1;
			break;
		}
	}

	return ret;
}

