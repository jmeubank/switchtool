extern "C" {
#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#endif
}

#include "terminal.hpp"


const int NETWK_TIMEOUT_SECONDS = 30;

static const telnet_telopt_t my_telopts[] = {
	{TELNET_TELOPT_ECHO, TELNET_WONT, TELNET_DO},
    {-1, 0, 0}
};

int Terminal::s_libssh_init_ct = 0;


void Terminal::TelnetEventHandler(telnet_t* telnet, telnet_event_t* ev, void* ud) {
	Terminal* t = static_cast< Terminal* >(ud);
	switch (ev->type) {
		case TELNET_EV_DATA:
			for (size_t i = 0; i < ev->data.size; ++i)
				t->m_telbuffer.push(ev->data.buffer[i]);
			break;
		case TELNET_EV_SEND:
			{
#ifdef WIN32
				unsigned long argp = 0;
				ioctlsocket(t->m_sock, FIONBIO, &argp);
#else
				int fl = fcntl(t->m_sock, F_GETFL);
				fl |= O_NONBLOCK;
				fcntl(t->m_sock, F_SETFL, fl);
#endif
				send(t->m_sock, ev->data.buffer, ev->data.size, 0);
			}
			break;
		case TELNET_EV_ERROR:
			throw fmt("TELNET error: %s", ev->error.msg);
			break;
		default:
			break;
	}
}


Terminal::Terminal(Protocol proto, const std::string& ip,
const PropTree& p_auth, const std::string& prompt_regex,
const std::string& continuation_regex)
	: m_proto(proto),
	m_prompt_regex(prompt_regex),
	m_cont_regex(continuation_regex),
	m_sock(0),
	m_ssh_session(0),
	m_ssh_channel(0),
	m_tel(0)
{
	PropTree auth_tree = p_auth;
	if (proto != PROTO_NETCONF_SSH && prompt_regex.length() <= 0)
		throw std::string("Must supply a prompt regex");

	m_sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	int portnum = 23;
	if (auth_tree.ChildExists("port"))
		portnum = atoi(auth_tree["port"].GetData().c_str());
	else if (proto == PROTO_SSH)
		portnum = 22;
	else if (proto == PROTO_NETCONF_SSH)
		portnum = 830;
	sin.sin_port = htons(portnum);
	sin.sin_addr.s_addr = inet_addr(ip.c_str());
	if (connect(m_sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) != 0)
		throw fmt("Failed to connect to %s on port %d", ip.c_str(), portnum);

	if (proto == PROTO_SSH || proto == PROTO_NETCONF_SSH) {
		if (s_libssh_init_ct <= 0) {
			int rc = libssh2_init(0);
			if (rc != 0)
				throw fmt("Failed to initialize libssh2: %d", rc);
		}
		++s_libssh_init_ct;

		m_ssh_session = libssh2_session_init();
		if (libssh2_session_startup(m_ssh_session, m_sock))
			throw fmt("Failed to establish SSH session");
		libssh2_keepalive_config(m_ssh_session, 1, 1);

		if (auth_tree["auth"].GetData() == "userpass") {
			if (libssh2_userauth_password(m_ssh_session,
			auth_tree["username"].GetData().c_str(),
			auth_tree["password"].GetData().c_str()) != 0)
				throw fmt("Authentication by password failed");
		} else if (auth_tree["auth"].GetData() == "rsa") {
			if (libssh2_userauth_publickey_fromfile(m_ssh_session,
			auth_tree["username"].GetData().c_str(),
			auth_tree["public-key-file"].GetData().c_str(),
			auth_tree["private-key-file"].GetData().c_str(), "")  != 0)
				throw fmt("Authentication by RSA key failed");
		} else {
			throw fmt("Invalid auth method: '%s'",
			auth_tree["auth"].GetData().c_str());
		}

		if (!(m_ssh_channel = libssh2_channel_open_session(m_ssh_session)))
			throw fmt("Unable to open a channel");

		if (proto == PROTO_SSH) {
			if (libssh2_channel_request_pty(m_ssh_channel, "vanilla") != 0)
				throw fmt("Failed requesting pty on channel");
			if (libssh2_channel_shell(m_ssh_channel) != 0)
				throw fmt("Unable to request shell on allocated pty");
		} else { //PROTO_NETCONF_SSH
			if (libssh2_channel_subsystem(m_ssh_channel, "netconf") != 0)
				throw fmt("Failed requesting NETCONF on channel");
		}
	} else {
		m_tel = telnet_init(my_telopts, TelnetEventHandler, 0, this);
		if (!m_tel)
			throw fmt("Failed to allocate libtelnet handler");
	}

	if (proto != PROTO_NETCONF_SSH) {
		std::string buf;
		while (!m_prompt_regex.FullMatch(buf.c_str())) {
			char c = GetChar();
			if (c == '\r' || c == '\n')
				buf.clear();
			else
				buf += c;
		}
	}
}
Terminal::~Terminal() {
	if (m_ssh_channel)
		libssh2_channel_free(m_ssh_channel);
	if (m_ssh_session) {
		libssh2_session_disconnect(m_ssh_session,
		"Normal Shutdown, Thank you for playing");
		libssh2_session_free(m_ssh_session);
	}
	--s_libssh_init_ct;
	if (s_libssh_init_ct <= 0)
		libssh2_exit();
	if (m_tel)
		telnet_free(m_tel);
	if (m_sock)
#ifdef WIN32
		closesocket(m_sock);
#else
		close(m_sock);
#endif
}

void Terminal::SetPromptRegex(const std::string& reg) {
	m_prompt_regex = pcrecpp::RE(reg);
}

void Terminal::SetContinuationRegex(const std::string& reg) {
	m_cont_regex = pcrecpp::RE(reg);
}

void Terminal::Execute(const std::string& cmd, DataCallback* dcb) {
	if (m_proto == PROTO_NETCONF_SSH) {
		SendTerm(cmd + "]]>]]>");
		std::string buf;
		size_t blen;
		while (true) {
			buf += GetChar();
			blen = buf.length();
			if (blen >= 6
			&& buf[blen - 6] == ']'
			&& buf[blen - 5] == ']'
			&& buf[blen - 4] == '>'
			&& buf[blen - 3] == ']'
			&& buf[blen - 2] == ']'
			&& buf[blen - 1] == '>')
				break;
		}
		buf.erase(blen - 6);
		if (dcb)
			dcb->OnData(buf);
	} else {
		SendTerm(cmd + "\r");
		while (GetChar() != '\n')
			;
		std::string buf;
		char c;
		while (true) {
			c = GetChar();
			if (c == 8) {
				if (buf.length() > 0)
					buf.erase(buf.length() - 1);
				continue;
			}
			if (c == 0) {
				buf.clear();
				continue;
			}
			if (c == '\n') {
				if (dcb)
					dcb->OnData(buf);
				buf.clear();
				continue;
			}
			if (c == '\r')
				continue;
			buf += c;
			if (m_prompt_regex.FullMatch(buf.c_str()))
				break;
			if (m_cont_regex.FullMatch(buf.c_str()))
				SendTerm(" ");
		}
	}
}

char Terminal::GetChar() {
	if (m_proto == PROTO_SSH || m_proto == PROTO_NETCONF_SSH) {
		int tmp;
		char c;
		while (true) {
			ssize_t ret = libssh2_channel_read(m_ssh_channel, &c, 1);
			if (ret == 1) {
				//fputc(c, stdout);
				return c;
			}
			if (ret != LIBSSH2_ERROR_EAGAIN)
				throw fmt("No more chars to read (SSH)");
			libssh2_keepalive_send(m_ssh_session, &tmp);
			struct timeval timeout;
			timeout.tv_sec = NETWK_TIMEOUT_SECONDS;
			timeout.tv_usec = 0;
			fd_set fd;
			FD_ZERO(&fd);
			FD_SET(m_sock, &fd);
			fd_set *writefd = NULL;
			fd_set *readfd = NULL;
			int dir = libssh2_session_block_directions(m_ssh_session);
			if(dir & LIBSSH2_SESSION_BLOCK_INBOUND)
				readfd = &fd;
			if(dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
				writefd = &fd;
			int rc = select(m_sock + 1, readfd, writefd, NULL, &timeout);
			if (rc <= 0)
				throw fmt("Timeout or error waiting for data (SSH)");
		}
	} else { //PROTO_TELNET
		unsigned long argp = 1;
		char c;
		while (m_telbuffer.size() <= 0) {
#ifdef WIN32
			ioctlsocket(m_sock, FIONBIO, &argp);
#else
			int fl = fcntl(m_sock, F_GETFL);
			fl &= ~O_NONBLOCK;
			fcntl(m_sock, F_SETFL, fl);
#endif
			int ret = recv(m_sock, &c, 1, 0);
			if (ret == 1) {
				telnet_recv(m_tel, &c, 1);
				continue;
			}
#ifdef WIN32
			if (ret != SOCKET_ERROR || WSAGetLastError() != WSAEWOULDBLOCK)
#else
			if (errno != EAGAIN && errno != EWOULDBLOCK)
#endif
				throw fmt("No more chars to read (telnet)");
			struct timeval timeout;
			timeout.tv_sec = NETWK_TIMEOUT_SECONDS;
			timeout.tv_usec = 0;
			fd_set fd;
			FD_ZERO(&fd);
			FD_SET(m_sock, &fd);
			fd_set *writefd = NULL;
			fd_set *readfd = &fd;
			int rc = select(m_sock + 1, readfd, writefd, NULL, &timeout);
			if (rc <= 0)
				throw fmt("Timeout or error waiting for data (telnet)");
		}
		c = m_telbuffer.front();
		m_telbuffer.pop();
		return c;
	}
}

void Terminal::SendTerm(const std::string& snd) {
	if (m_proto == PROTO_SSH || m_proto == PROTO_NETCONF_SSH) {
		libssh2_channel_set_blocking(m_ssh_channel, 1);
		libssh2_channel_write(m_ssh_channel, snd.c_str(), snd.length());
		libssh2_channel_set_blocking(m_ssh_channel, 0);
	} else //PROTO_TELNET
		telnet_send(m_tel, snd.c_str(), snd.length());
}
