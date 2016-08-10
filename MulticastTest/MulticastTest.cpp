/*
 *	This is a derived work from Juan-Mariano de Goyeneche, porting 
 *	his test program to Windows.
 *	
 *	(c) 2016, Michael Becker
 */
/*
 * This is free software released under the GPL license.
 * See the GNU GPL for details.
 *
 * (c) Juan-Mariano de Goyeneche. 1998, 1999.
 *
 *  Listing 1. Multicast Application
 *  http://www.linuxjournal.com/files/linuxjournal.com/linuxjournal/articles/030/3041/3041l1.html
 */
#include <stdio.h>
#include <stdlib.h> 

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Winsock2.h>
#include <Ws2tcpip.h>

#include <errno.h>
#include <string.h>
#include <string>
#include <iostream>


#define MAXLEN 1024
#define DELAY 2
#define DEFAULT_TTL 1


using namespace std;


unsigned short int port;
unsigned long group_address;


DWORD WINAPI RecevingThread(_In_ LPVOID lpParameter)
{
	int n;
	int len;
	struct sockaddr_in from;
	char message[MAXLEN + 1];
	struct sockaddr_in mcast_group;
	int recv_s;
	struct ip_mreq mreq;
	BOOL ReuseAddress = TRUE;
	DWORD NoMulticastLoopback = FALSE;

	if ((recv_s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		cout << "recv socket error " << WSAGetLastError() << endl;
		exit(1);
	}

	if (setsockopt(recv_s, SOL_SOCKET, SO_REUSEADDR, (const char *)&ReuseAddress, sizeof(ReuseAddress)) < 0) {
		cout << "reuseaddr setsockopt error " << WSAGetLastError() << endl;
		exit(1);
	}

	/* Disable Loop-back */
	if (setsockopt(recv_s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&NoMulticastLoopback, sizeof(NoMulticastLoopback)) < 0) {
		cout << "loop setsockopt error " << WSAGetLastError() << endl;
		exit(1);
	}

	memset(&mcast_group, 0, sizeof(mcast_group));
	mcast_group.sin_family = AF_INET;
	mcast_group.sin_addr.s_addr = htonl(INADDR_ANY);
	mcast_group.sin_port = htons(port);


	if (bind(recv_s, (struct sockaddr*)&mcast_group, sizeof(mcast_group)) < 0) {
		cout << "bind error " << WSAGetLastError() << endl;
		exit(1);
	}

	mreq.imr_multiaddr.s_addr = group_address;
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(recv_s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
		cout << "add_membership setsockopt error " << WSAGetLastError() << endl;
		exit(1);
	}

	for (;;) {

		len = sizeof(from);
		if ((n = recvfrom(recv_s, message, MAXLEN, 0, (struct sockaddr*)&from, &len)) < 0) {
			perror("recv");
			exit(1);
		}

		message[n] = 0; /* null-terminate string */
		printf("Received message from %s.\n", inet_ntoa(from.sin_addr));
		printf("\t%s", message);
	}
}


//	
//	https://msdn.microsoft.com/en-us/library/windows/desktop/ms740476(v=vs.85).aspx
//
//	Major compatibility issues:
//	https://support.microsoft.com/en-us/kb/257460
//	http://forums.codeguru.com/showthread.php?309487-Problems-with-multicast-sockets-in-Windows
//


int main(int argc, char* argv[])
{
	DWORD NoMulticastLoopback = FALSE;
	int send_s;     
	DWORD ttl;
	int iResult;
	WSADATA wsaData;

	if ((argc<3) || (argc>4)) {
		fprintf(stderr, "Usage: %s mcast_group port [ttl]\n", argv[0]);
		exit(1);
	}

	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR) {
		cout << "WSAStartup failed with error " << iResult << endl;
		return 1;
	}


	port = (unsigned short int)strtol(argv[2], NULL, 0);
	group_address = inet_addr(argv[1]);

	if ((send_s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		cout << "send socket error " << WSAGetLastError() << endl;
		exit(1);
	}

	/* If ttl supplied, set it */
	if (argc == 4) {
		ttl = strtol(argv[3], NULL, 0);
	}
	else {
		ttl = DEFAULT_TTL;
	}

	if (setsockopt(send_s, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl)) < 0) {
		cout << "ttl setsockopt error " << WSAGetLastError() << endl;
		exit(1);
	}

	/* Disable Loop-back */
	if (setsockopt(send_s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&NoMulticastLoopback, sizeof(NoMulticastLoopback)) < 0) {
		cout << "loop setsockopt error " << WSAGetLastError() << endl;
		exit(1);
	}

	//	
	//	We create a new thread, not fork() on Windows.
	//
	CreateThread(NULL, 0, RecevingThread, NULL, 0, NULL);

	struct sockaddr_in mcast_group;
	memset(&mcast_group, 0, sizeof(mcast_group));
	mcast_group.sin_family = AF_INET;
	mcast_group.sin_addr.s_addr = group_address;
	mcast_group.sin_port = htons(port);

	for (;;) {
		string line;
		size_t len = 0;

		do {
			getline(cin, line);

		} while (line.length() <= 0);

		int rc = sendto(send_s, line.c_str(), line.length(), 0, (struct sockaddr*)&mcast_group, sizeof(mcast_group));
		if ((rc == SOCKET_ERROR) && rc < (int)line.length()) {
			cout << "sendto error " << WSAGetLastError() << endl;
			exit(1);
		}
	}
}

