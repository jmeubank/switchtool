#ifndef COMMON_HPP_INC
#define COMMON_HPP_INC


#include <string>
#include <map>
#include <vector>
#include "proptree.hpp"


enum Protocol {
	PROTO_TELNET = 0,
	PROTO_SSH,
	PROTO_NETCONF_SSH,
	PROTO_SNMP1,
	PROTO_SNMP2
};
enum AuthType {
	AUTH_USERPASS = 0,
	AUTH_RSA = 1,
	AUTH_CONSOLE_SECRET = 2
};

struct AuthSet {
};

struct AccessMethod {
	int port;
	AuthType auth;
	std::string username;
	std::string password;
	std::string enable_secret;
	std::string key_file_public;
	std::string key_file_private;
	std::string community;

	AccessMethod() :
	auth(AUTH_USERPASS),
	username("admin"),
	password("password"),
	community("public")
	{}
};
typedef std::map< Protocol, AccessMethod > AccessMethodMap;

class Boss {
public:
	Boss();
	~Boss();

	void SetTCP(int port);
	PropTree GetOp();
	void SendReady() const;
	void SendGoodbye() const;
	void SendError(std::string const& error) const;
	void SendLine(std::string const& data) const;
	void SendOutputFinished() const;
	void SendPropTree(std::string const& name, const PropTree& proptree) const;
	void SendData(std::string const& data) const {
		this->Send((data + "\n").c_str(), data.length() + 1);
	}

private:
	void Send(const char* snd, size_t len) const;

	int m_sock;
};


std::string fmt(const char* msg, ...);

#endif
