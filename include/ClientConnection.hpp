#ifndef CLIENTCONNECTION_HPP
#define CLIENTCONNECTION_HPP

#include <string>
#include <ctime>
#include <sys/types.h>

class ClientConnection {
public:
	enum State {
		READING_REQUEST,
		CGI_RUNNING,
		SENDING_RESPONSE
	};

	int fd;
	size_t serverIndex;
	State state;

	std::string requestBuffer;
	std::string responseBuffer;
	size_t bytesSent;

	bool headersComplete;
	size_t headerEndOffset;
	size_t bodyBytesReceived;
	size_t maxBodySize;

	pid_t cgiPid;
	int cgiInputFd;
	int cgiOutputFd;
	std::string cgiBody;
	size_t cgiBodyOffset;
	std::string cgiOutputBuffer;
	std::string cgiScriptName;
	time_t cgiStartTime;

	ClientConnection(int socket, size_t servIdx = 0);
	~ClientConnection();

	void clearBuffers();
	bool isResponseComplete() const;
	size_t getRemainingBytes() const;
	void resetCgiState();
	bool isCgiActive() const;
};

#endif
