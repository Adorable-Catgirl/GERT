#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#pragma comment(lib, "Ws2_32.lib")

typedef int socklen_t;
#else
#include <sys/socket.h>
#endif

#include "netty.h"
#include "routeManager.h"
#include "logging.h"
#include <fcntl.h>
#include <map>
#include "Poll.h"
#include "API.h"

using namespace std;

map<IP, Peer*> peers;

extern Poll peerPoll;

enum Commands {
	REGISTERED,
	UNREGISTERED,
	ROUTE,
	RESOLVE,
	UNRESOLVE,
	LINK,
	UNLINK,
	CLOSEPEER,
	QUERY
};

void sockError(SOCKET * sock, char * err, Peer* me) {
	send(*sock, err, 3, 0);
	destroy(sock);
	throw 1;
}

Peer::Peer(void * sock) : Connection(sock) {
	SOCKET * newSocket = (SOCKET*)sock;
	char buf[3];

#ifdef _WIN32
	ioctlsocket(*newSocket, FIONBIO, &nonZero);
#else
	int flags = fcntl(*newSocket, F_GETFL);
	fcntl(*newSocket, F_SETFL, flags | O_NONBLOCK);
#endif

	recv(*newSocket, buf, 3, 0);
	log((string)"GEDS using " + to_string(buf[0]) + "." + to_string(buf[1]) + "." + to_string(buf[2]));
	UCHAR major = buf[0]; //Major version number
	if (major != vers.major) { //Determine if major number is not supported
		char error[3] = { 0, 0, 0 };
		sockError(newSocket, error, this); //This is me :D
	}
	else { //Major version found
		sockaddr remotename;
		getpeername(*newSocket, &remotename, (socklen_t*)&iplen);
		sockaddr_in remoteip = *(sockaddr_in*)&remotename;
		id = getKnown(remoteip);
		if (id == nullptr) {
			char error[3] = { 0, 0, 1 }; //STATUS ERROR NOT_AUTHORIZED
			sockError(newSocket, error, this);
		}
		peers[id->addr] = this;
		log("Peer connected from " + id->addr.stringify());
		processGEDS(this);
	}
};

Peer::~Peer() {
	killAssociated(this);
	peers.erase(this->id->addr);

	peerPoll.remove(*(SOCKET*)sock);

	destroy((SOCKET*)this->sock);
	log("Peer " + this->id->addr.stringify() + " disconnected");
}

Peer::Peer(void * socket, KnownPeer * known) : Connection(socket), id(known) {
	peers[id->addr] = this;
}

void Peer::close() {
	this->transmit(string({ CLOSEPEER })); //SEND CLOSE REQUEST
	delete this;
}

void Peer::transmit(string data) {
	send(*(SOCKET*)this->sock, data.c_str(), (ULONG)data.length(), 0);
}
