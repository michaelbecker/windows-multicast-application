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
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h> 

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <iphlpapi.h>

#include <errno.h>
#include <string.h>
#include <string>
#include <iostream>
#include <list>


#define MAXLEN 1024
#define DELAY 2
#define DEFAULT_TTL 1


using namespace std;

//
//	These are global to this program.
//
unsigned short int multicastGroupPort;
unsigned long multicastGroupAddress;
DWORD multicastTtl;


//
//	We want to handle multiple interfaces, so we'll 
//	encapsulate this in a simple class.
//
class MulticastInterface 
{
	public:
		MulticastInterface(MIB_IPADDRROW row);
		static void SendToAll(string message);

	private:
		int Socket;
		MIB_IPADDRROW ipAddrRow;
		static list<MulticastInterface *>multicastInterfaces;
		static DWORD WINAPI RecevingThread(_In_ LPVOID lpParameter);
		void Send(string message);
};

list<MulticastInterface *>MulticastInterface::multicastInterfaces;


//
//	Our ctor, try and do most of the socket setup here.
//
MulticastInterface::MulticastInterface(MIB_IPADDRROW row)
	: ipAddrRow(row)
{
	DWORD NoMulticastLoopback = FALSE;
	BOOL ReuseAddress = TRUE;

	if ((Socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP)) < 0) {
		cout << "socket error " << WSAGetLastError() << endl;
		exit(1);
	}

	//
	//	We need to explicitly join the interface we were passed in here.
	//
	if (setsockopt(Socket, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&row.dwAddr, sizeof(row.dwAddr)) < 0) {
		cout << "IP_MULTICAST_IF setsockopt error " << WSAGetLastError() << endl;
		exit(1);
	}

	//
	//	Set our time-to-live
	//
	if (setsockopt(Socket, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&multicastTtl, sizeof(multicastTtl)) < 0) {
		cout << "ttl setsockopt error " << WSAGetLastError() << endl;
		exit(1);
	}

	//
	//	Disable Loop-back 
	//
	if (setsockopt(Socket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&NoMulticastLoopback, sizeof(NoMulticastLoopback)) < 0) {
		cout << "loop setsockopt error " << WSAGetLastError() << endl;
		exit(1);
	}

	//
	//	We want to be able to reopen this address if we restart the program.
	//
	if (setsockopt(Socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&ReuseAddress, sizeof(ReuseAddress)) < 0) {
		cout << "reuseaddr setsockopt error " << WSAGetLastError() << endl;
		exit(1);
	}

	//	
	//	It's easier to create a new thread on Windows,
	//	instead of fork'ing a new process.
	//
	CreateThread(NULL, 0, RecevingThread, this, 0, NULL);

	//
	//	Save ourselves on a global list.
	//
	multicastInterfaces.push_front(this);
};


//
//	Sends a string out a single interface.
//
void MulticastInterface::Send(string message)
{
	struct sockaddr_in mcast_group;

	memset(&mcast_group, 0, sizeof(mcast_group));
	mcast_group.sin_family = AF_INET;
	mcast_group.sin_addr.s_addr = multicastGroupAddress;
	mcast_group.sin_port = htons(multicastGroupPort);

	struct in_addr localAddress;
	localAddress.S_un.S_addr = ipAddrRow.dwAddr;

	//
	//	inet_ntoa() uses internal memory for the string, so embedding two calls to 
	//	it in the same printf call won't work.
	//
	printf("\tSending message from %s ", inet_ntoa(localAddress));
	printf(" to %s.\n", inet_ntoa(mcast_group.sin_addr));

	int rc = sendto(Socket, message.c_str(), message.length(), 0, (struct sockaddr*)&mcast_group, sizeof(mcast_group));
	if ((rc == SOCKET_ERROR) && rc < (int)message.length()) {
		cout << "sendto error " << WSAGetLastError() << endl;
		exit(1);
	}
}


//
//	Sends a string on "all" interfaces.
//
void MulticastInterface::SendToAll(string message)
{
	for (list<MulticastInterface *>::iterator it = multicastInterfaces.begin();
		it != multicastInterfaces.end();
		it++) {

		MulticastInterface *mi = *it;
		mi->Send(message);
	}
	printf("\n");
}


//
//	Each interface has a receiving thread, which independently handles
//	reading data from it's multicast socket.
//
DWORD WINAPI MulticastInterface::RecevingThread(_In_ LPVOID lpParameter)
{
	int n;
	int len;
	struct sockaddr_in from;
	char message[MAXLEN + 1];
	struct sockaddr_in mcast_group;
	struct ip_mreq mreq;
	
	MulticastInterface *mi = (MulticastInterface *)lpParameter;

	memset(&mcast_group, 0, sizeof(mcast_group));
	mcast_group.sin_family = AF_INET;
	mcast_group.sin_addr.s_addr = htonl(INADDR_ANY);
	mcast_group.sin_port = htons(multicastGroupPort);


	if (bind(mi->Socket, (struct sockaddr*)&mcast_group, sizeof(mcast_group)) < 0) {
		cout << "bind error " << WSAGetLastError() << endl;
		exit(1);
	}

	mreq.imr_multiaddr.s_addr = multicastGroupAddress;
	//mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	mreq.imr_interface.s_addr = mi->ipAddrRow.dwAddr;

	if (setsockopt(mi->Socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
		cout << "add_membership setsockopt error " << WSAGetLastError() << endl;
		exit(1);
	}

	for (;;) {

		len = sizeof(from);
		if ((n = recvfrom(mi->Socket, message, MAXLEN, 0, (struct sockaddr*)&from, &len)) < 0) {
			perror("recv");
			exit(1);
		}

		message[n] = 0; /* null-terminate string */
		printf("\tReceived message from %s.\n", inet_ntoa(from.sin_addr));
		printf("\t%s", message);
	}
}


//	
//	References:
//	
//	https://msdn.microsoft.com/en-us/library/windows/desktop/ms740476(v=vs.85).aspx
//
//	Major compatibility issues:
//	https://support.microsoft.com/en-us/kb/257460
//	http://forums.codeguru.com/showthread.php?309487-Problems-with-multicast-sockets-in-Windows
//


int main(int argc, char* argv[])
{
	int iResult;
	WSADATA wsaData;

	if ((argc<3) || (argc>4)) {
		fprintf(stderr, "Usage: %s MulticastTest port [ttl]\n", argv[0]);
		exit(1);
	}

	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR) {
		cout << "WSAStartup failed with error " << iResult << endl;
		return 1;
	}

	multicastGroupPort = (unsigned short int)strtol(argv[2], NULL, 0);
	multicastGroupAddress = inet_addr(argv[1]);

	/* If ttl supplied, set it */
	if (argc == 4) {
		multicastTtl = strtol(argv[3], NULL, 0);
	}
	else {
		multicastTtl = DEFAULT_TTL;
	}

	//	
	//	Find adapters - from MS example code TestGetIpAddrTable.cpp
	//
	/* Variables used by GetIpAddrTable */
	PMIB_IPADDRTABLE pIPAddrTable;
	DWORD dwSize = 0;
	DWORD dwRetVal = 0;

	/* Variables used to return error message */
	LPVOID lpMsgBuf;

	// Before calling AddIPAddress we use GetIpAddrTable to get
	// an adapter to which we can add the IP.
	pIPAddrTable = (MIB_IPADDRTABLE *)malloc(sizeof(MIB_IPADDRTABLE));

	if (pIPAddrTable) {
		// Make an initial call to GetIpAddrTable to get the
		// necessary size into the dwSize variable
		if (GetIpAddrTable(pIPAddrTable, &dwSize, 0) == ERROR_INSUFFICIENT_BUFFER) {
			free(pIPAddrTable);
			pIPAddrTable = (MIB_IPADDRTABLE *)malloc(dwSize);
		}
		if (pIPAddrTable == NULL) {
			printf("Memory allocation failed for GetIpAddrTable\n");
			exit(1);
		}
	}

	// Make a second call to GetIpAddrTable to get the
	// actual data we want
	if ((dwRetVal = GetIpAddrTable(pIPAddrTable, &dwSize, 0)) != NO_ERROR) {

		printf("GetIpAddrTable failed with error %d\n", dwRetVal);

		if (FormatMessage(	FORMAT_MESSAGE_ALLOCATE_BUFFER | 
							FORMAT_MESSAGE_FROM_SYSTEM | 
							FORMAT_MESSAGE_IGNORE_INSERTS, 
							NULL, 
							dwRetVal, 
							MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),// Default language
							(LPTSTR)& lpMsgBuf, 
							0, 
							NULL)) {
			printf("\tError: %s", (char *)lpMsgBuf);
			LocalFree(lpMsgBuf);
		}
		exit(1);
	}

	printf("\tRaw Number of Interfaces with IP Addresses: %ld\n", pIPAddrTable->dwNumEntries);

	for (int i = 0; i < (int)pIPAddrTable->dwNumEntries; i++) {

		IN_ADDR IPAddr;
		IN_ADDR LoopbackAddr;
		LoopbackAddr.S_un.S_un_b.s_b1 = 127;
		LoopbackAddr.S_un.S_un_b.s_b2 = 0;
		LoopbackAddr.S_un.S_un_b.s_b3 = 0;
		LoopbackAddr.S_un.S_un_b.s_b4 = 1;

		bool CreateMulticastInterface = true;

		//printf("\n\tInterface Index[%d]:\t%ld\n", i, pIPAddrTable->table[i].dwIndex);
		IPAddr.S_un.S_addr = (u_long)pIPAddrTable->table[i].dwAddr;

		//
		//	Skip the loopback address.
		//
		if (IPAddr.S_un.S_addr == LoopbackAddr.S_un.S_addr) {
			printf("\n\tSkipping loopback address...\n");
			continue;
		}

		printf("\n\tIP Address[%d]:     \t%s\n", i, inet_ntoa(IPAddr));
		IPAddr.S_un.S_addr = (u_long)pIPAddrTable->table[i].dwMask;
		printf("\tSubnet Mask[%d]:    \t%s\n", i, inet_ntoa(IPAddr));
		//IPAddr.S_un.S_addr = (u_long)pIPAddrTable->table[i].dwBCastAddr;
		//printf("\tBroadCast[%d]:      \t%s (%ld%)\n", i, inet_ntoa(IPAddr), pIPAddrTable->table[i].dwBCastAddr);
		//printf("\tReassembly size[%d]:\t%ld\n", i, pIPAddrTable->table[i].dwReasmSize);

		printf("\tType and State[%d]:", i);
		if (pIPAddrTable->table[i].wType & MIB_IPADDR_PRIMARY) {
			printf("\tPrimary IP Address");
		}
		if (pIPAddrTable->table[i].wType & MIB_IPADDR_DYNAMIC) {
			printf("\tDynamic IP Address");
		}
		if (pIPAddrTable->table[i].wType & MIB_IPADDR_DISCONNECTED) {
			printf("\tAddress is on disconnected interface");
			CreateMulticastInterface = false;
		}
		if (pIPAddrTable->table[i].wType & MIB_IPADDR_DELETED) {
			printf("\tAddress is being deleted");
			CreateMulticastInterface = false;
		}
		if (pIPAddrTable->table[i].wType & MIB_IPADDR_TRANSIENT) {
			printf("\tTransient address");
			CreateMulticastInterface = false;
		}
		printf("\n");

		//
		//	Create a Multicast interface object for every valid IPv4 
		//	Interface we find.
		//
		if (CreateMulticastInterface) {
			MulticastInterface *mi = new MulticastInterface(pIPAddrTable->table[i]);
		}
	}

	//
	//	Clean up
	//
	free(pIPAddrTable);
	pIPAddrTable = NULL;

	//
	//	Now that we are setup, sit in a loop reading text input
	//	and sending it to every multicast output.
	//
	for (;;) {
		string line;
		size_t len = 0;

		do {
			getline(cin, line);
		} while (line.length() <= 0);

		MulticastInterface::SendToAll(line);
	}
}

