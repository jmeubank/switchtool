
#include <string>
#include <map>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>

extern "C" {
#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif
}

#include "common.hpp"
#include "host.hpp"
#include "yajl/yajl_gen.h"


std::string fmt(const char* msg, ...) {
	static char buf[1024];
	va_list args;
	va_start(args, msg);
	vsnprintf(buf, 1024, msg, args);
	va_end(args);
	buf[1023] = '\0';
	return buf;
}

std::string escapeJsonString(const std::string& input) {
	std::ostringstream ss;
	for (std::string::const_iterator iter = input.begin();
	iter != input.end();
	iter++) {
		switch (*iter) {
			case '\\': ss << "\\\\"; break;
			case '"': ss << "\\\""; break;
			case '/': ss << "\\/"; break;
			case '\b': ss << "\\b"; break;
			case '\f': ss << "\\f"; break;
			case '\n': ss << "\\n"; break;
			case '\r': ss << "\\r"; break;
			case '\t': ss << "\\t"; break;
			default: ss << *iter; break;
		}
	}
	return ss.str();
}


Boss::Boss() :
	m_sock(0)
{}
Boss::~Boss() {
	if (m_sock)
#ifdef WIN32
		closesocket(m_sock);
#else
		close(m_sock);
#endif
}
void Boss::SetTCP(int port) {
	m_sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (connect(m_sock, (struct sockaddr*)&sin, sizeof(struct sockaddr_in)) != 0)
		throw fmt("Failed to connect to 127.0.0.1:%d", port);
}
PropTree Boss::GetOp() {
	std::string buf;
	char ch;
	while (true) {
		if (m_sock != 0) {
			if (recv(m_sock, &ch, 1, 0) <= 0)
				throw fmt("EOF or error on boss TCP input");
		} else {
			if (fread(&ch, 1, 1, stdin) != 1)
				throw fmt("EOF or error on boss stdin input");
		}
		buf += ch;
		if (buf.length() >= 6
		&& buf[buf.length() - 1] == ':'
		&& buf[buf.length() - 2] == '}'
		&& buf[buf.length() - 3] == '}'
		&& buf[buf.length() - 4] == ':'
		&& buf[buf.length() - 5] == '}'
		&& buf[buf.length() - 6] == '}')
			break;
	}
	buf.erase(buf.length() - 6);
	return PropTree::FromJson(buf);
}
void Boss::Send(const char* snd, size_t len) const {
	if (m_sock != 0)
		send(m_sock, snd, len, 0);
	else {
		fwrite(snd, len, 1, stdout);
		fflush(stdout);
	}
}
void Boss::SendError(std::string const& error) const {
	std::string snd = std::string("{\"error\": \"") + escapeJsonString(error)
	+ "\"}\n}}:}}:\n";
	this->Send(snd.c_str(), snd.length());
}
void Boss::SendLine(std::string const& data) const {
	std::string snd = std::string("{\"line\": \"") + escapeJsonString(data)
	+ "\"}\n}}:}}:\n";
	this->Send(snd.c_str(), snd.length());
}
void Boss::SendOutputFinished() const {
	std::string snd = "{\"output-finished\": 1}\n}}:}}:\n";
	this->Send(snd.c_str(), snd.length());
}
void Boss::SendReady() const {
	std::string snd = "{\"ready\": 1}\n}}:}}:\n";
	this->Send(snd.c_str(), snd.length());
}
void Boss::SendGoodbye() const {
	std::string snd = "{\"goodbye\": 1}\n}}:}}:\n";
	this->Send(snd.c_str(), snd.length());
}
static void SendPropTreeRecursive(yajl_gen g, PropTree const& proptree) {
	if (!proptree.HasChildren()) {
		yajl_gen_string(
			g,
			reinterpret_cast< const unsigned char* >(proptree.GetData().c_str()),
			proptree.GetData().length()
		);
	} else if (proptree.IsArray()) {
		yajl_gen_array_open(g);
		for (PropTree::const_iterator it = proptree.Begin();
		it != proptree.End();
		++it)
			SendPropTreeRecursive(g, *it);
		yajl_gen_array_close(g);
	} else {
		yajl_gen_map_open(g);
		for (PropTree::const_iterator it = proptree.Begin();
		it != proptree.End();
		++it) {
			if (it.GetKey().length() <= 0)
				continue;
			yajl_gen_string(
				g,
				reinterpret_cast< const unsigned char* >(it.GetKey().c_str()),
				it.GetKey().length()
			);
			SendPropTreeRecursive(g, *it);
		}
		yajl_gen_map_close(g);
	}
}
void Boss::SendPropTree(std::string const& name,
PropTree const& proptree) const {
	yajl_gen g = yajl_gen_alloc(0);
	yajl_gen_config(g, yajl_gen_beautify, 1);
	yajl_gen_map_open(g);
	yajl_gen_string(
		g,
		reinterpret_cast< const unsigned char* >(name.c_str()),
		name.length()
	);
	SendPropTreeRecursive(g, proptree);
	yajl_gen_map_close(g);
	const unsigned char* buf;
	size_t len;
	yajl_gen_get_buf(g, &buf, &len);
	this->Send(reinterpret_cast< const char* >(buf), len);
	yajl_gen_clear(g);
	yajl_gen_free(g);
	std::string snd = "}}:}}:\n";
	this->Send(snd.c_str(), snd.length());
}


std::map< std::string, HostFactory* >* Host::GetFactories() {
	static std::map< std::string, HostFactory* > factories;
	return &factories;
}

Host* Host::Construct(const Boss& boss, const PropTree& phost) {
	std::string switchtype = phost["type"];
	std::map< std::string, HostFactory* >::const_iterator fd
	= GetFactories()->find(switchtype);
	if (fd == GetFactories()->end()) {
		throw fmt("No function library for switch type '%s'",
		switchtype.c_str());
	}
	return fd->second->Construct(boss, phost);
}


int main(int argc, char* argv[]) {
#ifdef WIN32
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2,0), &wsadata);
#endif

	Boss boss;

	try {
		if (argc > 1) {
			boss.SetTCP(atoi(argv[1]));
			boss.SendReady();
		}
		fputs("{\"ready\": 1}\n}}:}}:\n", stdout);
		fflush(stdout);
	} catch (std::string& e) {
		printf("-%s\n", e.c_str());
		return -1;
	}

	try {
		PropTree phost = boss.GetOp()["host"];
		Host* host = Host::Construct(boss, phost);
		PropTree op;
		while (true) {
			op = boss.GetOp();
			if (op.ChildExists("end"))
				break;
			if (!op.ChildExists("command"))
				throw std::string("Command expected");
			host->Execute(op["command"], op["args"]);
		}
		delete host;
		boss.SendGoodbye();
		return 0;
	} catch (std::string& e) {
		boss.SendError(e);
	} catch (std::exception& e) {
		boss.SendError(fmt("Uncaught exception: %s", e.what()));
	} catch (...) {
		boss.SendError("Uncaught unknown exception");
	}
	return -1;
}
