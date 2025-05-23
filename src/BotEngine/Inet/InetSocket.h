#ifndef __SOCKET_H
#define __SOCKET_H

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <poll.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>
#endif

#include <cstdlib>
#include <errno.h>
#include <cstring>
#include <iostream>

using namespace std;

const int MAX_HOSTNAME_LENGTH = 200;
const int MAX_RECV_LENGTH = 1024;
const int SOCKET_CONNECTION_LIMIT = 10;

class Socket {
public:
  Socket (void);
  Socket (const Socket& source) = delete;

  ~Socket (void);

  int bind (const char* port, const char* ip = NULL);
  Socket* accept (bool block = true);

  bool connected (void) const;
  int recvLength (void) const;

  int close (void);
  int connect (const char* ip, const char* port);
  int receive (char* buffer, int bufferLength, int timeout = 30);
  int send (const char* buffer, int bufferLength, bool critical = false);

private:
  Socket (int sock);

  void checkForReady (short events, int timeout);

  bool mConnected;
  bool mInvalid;
  bool mReadReady;
  bool mReadyReadyOOB;
  bool mWriteReady;
  bool mTimedout;
  int mSocket;
  int mReceivedBytes;
};

#endif
