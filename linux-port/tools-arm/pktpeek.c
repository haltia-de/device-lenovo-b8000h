/* pktpeek IFACE [N]: print ethertype and IPv4/ARP addresses of N received frames. */
#include <arpa/inet.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct sockaddr_ll sll = { .sll_family = AF_PACKET, .sll_protocol = htons(ETH_P_ALL) };
	struct timeval tv = { .tv_sec = 20 };
	unsigned char b[2048];
	int fd, n, i, max = argc > 2 ? atoi(argv[2]) : 30;

	if (argc < 2)
		return 2;
	fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	sll.sll_ifindex = if_nametoindex(argv[1]);
	if (fd < 0 || bind(fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
		perror("socket");
		return 1;
	}
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	for (i = 0; i < max; i++) {
		struct sockaddr_ll from;
		socklen_t fl = sizeof(from);
		unsigned t;

		n = recvfrom(fd, b, sizeof(b), 0, (struct sockaddr *)&from, &fl);
		if (n < 14)
			break;
		t = b[12] << 8 | b[13];
		printf("%s %02x:%02x:%02x:%02x:%02x:%02x type %04x len %d",
		       from.sll_pkttype == PACKET_OUTGOING ? "OUT" : "IN ",
		       b[6], b[7], b[8], b[9], b[10], b[11], t, n);
		if (t == 0x0800 && n >= 34)
			printf(" ip %d.%d.%d.%d -> %d.%d.%d.%d proto %d", b[26], b[27], b[28],
			       b[29], b[30], b[31], b[32], b[33], b[23]);
		if (t == 0x0806 && n >= 42)
			printf(" arp op %d %d.%d.%d.%d -> %d.%d.%d.%d", b[21], b[28], b[29],
			       b[30], b[31], b[38], b[39], b[40], b[41]);
		printf("\n");
		fflush(stdout);
	}
	return 0;
}
