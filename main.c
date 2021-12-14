#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <pthread.h>
#include<string.h>
#include "dpdk.h"
#include <sys/syscall.h>
#include <signal.h>
#define RX_RING_SIZE 128
#define TX_RING_SIZE 512
#define NUM_MBUFS (1 << 23)
#define NUM_RX_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 8
#define ARRAY_LENGTH 10000000
#define SEND_PPS 300000000
#define GENTIME (1000000000 / SEND_PPS)
uint64_t rx_time[ARRAY_LENGTH];
FILE *fp;
uint64_t t1,t2;
//这里用skleten 默认配置
static const struct rte_eth_conf port_conf_default = {
	.rxmode = { .max_lro_pkt_size = RTE_ETHER_MAX_LEN }
};
void pin_to_cpu(int core){
	int ret;
	cpu_set_t cpuset;
	pthread_t thread;

	thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(core, &cpuset);
	ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (ret != 0)
	    printf("Cannot pin thread\n");
}


/*
 *这个是简单的端口初始化 
 *我在这里简单的端口0 初始化了一个 接收队列和一个发送队列
 *并且打印了一条被初始化的端口的MAC地址信息
 */
static inline int
port_init(struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = 1, tx_rings = 1;
	int retval;
	uint16_t q;


	/*配置端口0,给他分配一个接收队列和一个发送队列*/
	retval = rte_eth_dev_configure(0, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(0, q, RX_RING_SIZE,
				rte_eth_dev_socket_id(0), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(0, q, TX_RING_SIZE,
				rte_eth_dev_socket_id(0), NULL);
		if (retval < 0)
			return retval;
	}

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(0);
	if (retval < 0)
		return retval;

	return 0;
}
struct Request {
    uint64_t runNs;
    uint64_t genNs;
};
struct Response {
    uint64_t runNs;
    uint64_t genNs;
};
#define TX_NUM 1
struct th_tx_arg{
	int pin_to_cpu;
	int pin_tx_id;
	struct rte_mempool *mp;
}tx_arg[TX_NUM];

 #define ntoh16(x)	(rte_be_to_cpu_16(x))
#define ntoh32(x)	(rte_be_to_cpu_32(x))
#define ntoh64(x)	(rte_be_to_cpu_64(x))

#define hton16(x)	(rte_cpu_to_be_16(x))
#define hton32(x)	(rte_cpu_to_be_32(x))
#define hton64(x)	(rte_cpu_to_be_64(x))

uint64_t tx_num[TX_NUM] = { 0 };
uint64_t rx_num = 0;
struct rte_mempool *mbuf_pool;
struct rte_mempool *rx_mempool;
void sigint_handler(int sig) {
    
	for(int i = 1; i < rx_num; i++)
	{
		fwrite((void *)&rx_time[i], sizeof(uint64_t), 1, fp);
	}
	if(t2 == 0) t2 = getCurNs();
	t2 = (t2 - t1) / 1000000;
	uint64_t all_tx_num = 0;
	for(int i = 0; i < TX_NUM; i++) 
	{
		all_tx_num += tx_num[i];
		printf("per  cpu tx is %d\n", tx_num[i]);
	}
	printf("send time is %llu ms\n", t2);
	printf("pps is %lld\n", all_tx_num / t2);
	printf("\npackets_sent: %lu\n", all_tx_num);
    printf("packets_received: %lu\n", rx_num);
    syscall(SYS_exit_group, 0);
}
struct rte_mbuf * pkt[10000000];
void *pt_send(void *tx_arg)
{
	struct th_tx_arg *arg = (struct th_tx_arg *)tx_arg;
	printf("%d %d\n",arg->pin_to_cpu, arg->pin_tx_id);
	pin_to_cpu(arg->pin_to_cpu);
	struct rte_ether_addr s_addr = {{0xe4,0x43,0x4b,0xe6,0xbc,0x00}};
	struct rte_ether_addr d_addr = {{0xe4,0x43,0x4b,0x76,0x27,0x96}};
	uint16_t ether_type =hton16( 0x0800); 	
	uint16_t           eth_type,udp_port;
	uint32_t           ip_addr;
	struct rte_udp_hdr    *udp_h;
	struct rte_ipv4_hdr   *ip_h;
	struct rte_ether_hdr *eth_hdr;


	//对每个buf ， 给他们添加包
	#define PACKET_NUM 4000000
//	struct rte_mbuf * pkt[2];
	int i = 0;
	t1 = getCurNs();

	printf("%d\n",PACKET_NUM);
	for(i = 0; i < PACKET_NUM; i++)
	{
		
		pkt[i] = rte_pktmbuf_alloc(arg->mp);
		unsigned char *payload;
		payload = (unsigned char *)((uint64_t)pkt[i] + 298);
		struct Request *req = (struct Request *)payload;
		req->genNs = getCurNs() + GENTIME;
		req->runNs = 1000;
		eth_hdr = rte_pktmbuf_mtod(pkt[i],struct ether_hdr*);
		eth_hdr->dst_addr = d_addr;
		eth_hdr->src_addr = s_addr;
		eth_hdr->ether_type = ether_type;
		ip_h = (struct rte_ipv4_hdr*) (rte_pktmbuf_mtod(pkt[i],char*) + sizeof(struct rte_ether_hdr));
		ip_h->type_of_service = 0;
		ip_h->total_length = hton16(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + sizeof(struct Response));
		ip_h->packet_id = 0;
		ip_h->time_to_live = 64;
		ip_h->next_proto_id = IPPROTO_UDP;
		ip_h->hdr_checksum = 0;
		ip_h->src_addr = hton32(0xc0aa0002);
		ip_h->dst_addr = hton32(0xc0aa0001);
		ip_h->version_ihl = 4;
		ip_h->version_ihl = ip_h->version_ihl<<4 | sizeof(struct rte_ipv4_hdr) / 4; 
		ip_h->hdr_checksum = chksum_internet((void *)ip_h, sizeof(struct rte_ipv4_hdr));
		int pkt_size = sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_udp_hdr) + sizeof(struct Request);
		pkt[i]->data_len = pkt_size;
		pkt[i]->pkt_len = pkt_size;
		udp_h = (struct rte_udp_hdr *) ((char *) ip_h + sizeof(struct rte_ipv4_hdr));
		udp_h->src_port = hton16(5678);
		udp_h->dst_port = hton16(1234);
		udp_h->dgram_len = hton16(sizeof(struct Response) + sizeof(struct rte_udp_hdr));
		udp_h->dgram_cksum = 0;

//		while(getCurNs() < req->genNs);
//		uint16_t nb_tx = rte_eth_tx_burst(0,arg->pin_tx_id,pkt,1);
//		tx_num[arg->pin_tx_id] += nb_tx;
	//	printf("tx_num %d\n", tx_num);
	}
	printf("prepare finished!\n");
	printf("input to start!");
	if(getchar() == 'c') return;
	t1 = getCurNs();
	for(int i = 0; i < PACKET_NUM; i++)
	{
		unsigned char *payload;
		payload = (unsigned char *)((uint64_t)pkt[i] + 298);
		struct Request *req = (struct Request *)payload;
		req->genNs = getCurNs() + GENTIME;
		while(getCurNs() < req->genNs);
		uint16_t nb_tx = rte_eth_tx_burst(0,arg->pin_tx_id,&pkt[i],1);
		tx_num[arg->pin_tx_id] += nb_tx;
	}	
	t2 = getCurNs();
	printf("%llu %llu %llu\n", t1, t2, t2 - t1);
//	uint16_t nb_tx = rte_eth_tx_burst(0,0,pkt,BURST_SIZE);
//	printf("发送成功%d个包\n",nb_tx);
	//发送完成，答应发送了多少个
	
//	for(i=0;i<BURST_SIZE;i++)
//		rte_pktmbuf_free(pkt[i]);
}

void *pt_recv(void *c)
{
	pin_to_cpu(29);
	struct rte_ether_hdr * eth_hdr;
	struct rte_udp_hdr    *udp_h;
	struct rte_ipv4_hdr   *ip_h;
	struct Response *resp;
	uint64_t num_rx = 0;
	struct rte_mbuf *pkt[BURST_SIZE];
	for(;;)
	{
//		t2 = getCurNs();
//		printf("t1 : %llu t2 : %llu\n", t1, t2);
		int i;

		//从接受队列中取出包
		uint16_t nb_rx = rte_eth_rx_burst(0, 0,pkt,BURST_SIZE);
		
		if(nb_rx == 0)
		{
			//如果没有接受到就跳过
			continue;
		}
		//打印信息
		for(i=0;i<nb_rx;i++)
		{
			eth_hdr = rte_pktmbuf_mtod(pkt[i],struct rte_ether_hdr*);
			rx_num++;
			#ifdef DEBUG
			printf("收到包 来自MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
				   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " : %d\n",
				eth_hdr->src_addr.addr_bytes[0],eth_hdr->src_addr.addr_bytes[1],
				eth_hdr->src_addr.addr_bytes[2],eth_hdr->src_addr.addr_bytes[3],
				eth_hdr->src_addr.addr_bytes[4],eth_hdr->src_addr.addr_bytes[5], num_rx);
			#endif
			resp = (struct Response *)((char *)eth_hdr + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + sizeof(struct rte_ether_hdr));
			uint64_t time = getCurNs() - resp->genNs - 1000;
			rx_time[ rx_num%ARRAY_LENGTH ] = time;
//			printf("%llu now %lld gen %lld\n",time, getCurNs(), resp->genNs);
//			fwrite((void *)&time, sizeof(uint64_t), 1, fp);
			rte_pktmbuf_free(pkt[i]);
		}
		
	}
}
int main(int argc, char *argv[])
{
	
	fp = fopen("data", "w+");
	if(fp == 0)
	{
		printf("open failed!\n");
		return -1;
	}
	printf("mbuf :%d\n", sizeof(struct rte_mbuf));
	printf("%d\n",RTE_MBUF_DEFAULT_BUF_SIZE);
	pthread_t receiver, sender[TX_NUM];
	/*进行总的初始话*/
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "initlize fail!");

	//I don't clearly know this two lines
	argc -= ret;
	argv += ret;

	/* Creates a new mempool in memory to hold the mbufs. */
	//分配内存池
	mbuf_pool = rte_pktmbuf_pool_create("TX0_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, SOCKET_ID_ANY);
	rx_mempool = rte_pktmbuf_pool_create("RX0_POOL", NUM_RX_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, SOCKET_ID_ANY);

	//如果创建失败
	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initialize all ports. */
	//初始话端口设备 顺便给他们分配  队列
		if (port_init(rx_mempool) != 0)
			rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n",
					0);
	signal(SIGINT, sigint_handler);	
	printf("start work!\n");
	tx_arg[0].pin_to_cpu = 30;
	tx_arg[0].pin_tx_id = 0;
	tx_arg[0].mp = mbuf_pool;
//	tx_arg[1].pin_to_cpu = 31;
//	tx_arg[1].pin_tx_id = 1;
	pthread_create(&receiver, NULL, pt_recv, NULL);
	pthread_create(&sender[0], NULL, pt_send, &tx_arg);
//	pthread_create(&sender[1], NULL, pt_send, &tx_arg[1]);
	pthread_join(receiver, NULL);
	pthread_join(sender[0], NULL);
//	pthread_join(sender[1], NULL);
	//自己定义的包头
	

	return 0;
}
