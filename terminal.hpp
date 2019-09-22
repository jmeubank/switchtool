#ifndef TERMINAL_HPP_INC
#define TERMINAL_HPP_INC


#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
#endif

#include <cstdio>
#include <queue>
#include <pcrecpp.h>
extern "C" {
#include <libssh2.h>
#include "libtelnet/libtelnet.h"
}
#include "common.hpp"


struct DataCallback {
	virtual ~DataCallback() {}
	virtual void OnData(const std::string& data) = 0;
};

class Terminal {
public:
	static void TelnetEventHandler(telnet_t* telnet, telnet_event_t* ev, void* ud);

	Terminal(Protocol proto, const std::string& ip, const PropTree& p_auth,
	const std::string& prompt_regex = std::string(),
	const std::string& continuation_regex = std::string());
	~Terminal();

	void SetPromptRegex(const std::string& reg);
	void SetContinuationRegex(const std::string& reg);
	void Execute(const std::string& cmd, DataCallback* dcb = 0);

private:
	char GetChar();
	void SendTerm(const std::string& snd);

	static int s_libssh_init_ct;

	Protocol m_proto;
	pcrecpp::RE m_prompt_regex;
	pcrecpp::RE m_cont_regex;
#ifdef WIN32
	SOCKET m_sock;
#else
	int m_sock;
#endif
	LIBSSH2_SESSION* m_ssh_session;
	LIBSSH2_CHANNEL* m_ssh_channel;
	telnet_t* m_tel;
	std::queue< char > m_telbuffer;
};


#endif
