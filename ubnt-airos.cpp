#include <cmath>
#include <sstream>

#include "host.hpp"
#include "terminal.hpp"

class AirOS : public Host {
public:
	AirOS(const Boss& boss, const PropTree& phost);
	virtual ~AirOS();

	virtual void Execute(const std::string& cmd, const std::string& args);

private:
	void GetTerminal();

	Terminal* m_term;
};

static HostFactoryRegistrant< AirOS > r("airos");


struct AirOSCommandCB : public DataCallback {
	const Boss& boss;
	AirOSCommandCB(const Boss& b) :
		boss(b)
	{}
	virtual void OnData(const std::string& data) {
		if (data.substr(0, 6) == "failed")
			boss.SendError(data);
	}
};


AirOS::AirOS(const Boss& boss, const PropTree& phost)
	: Host(boss, phost),
	m_term(0) {
}
AirOS::~AirOS() {
	if (m_term)
		delete m_term;
}

void AirOS::Execute(const std::string& cmd, const std::string& args) {
	if (cmd == "passthru") {
		GetTerminal();
		struct DCB1 : public DataCallback {
			const Boss& boss;
			DCB1(const Boss& b) : boss(b) {}
			virtual void OnData(const std::string& data) {
				boss.SendLine(data);
			}
		} dcb1(m_boss);
		m_term->Execute(args, &dcb1);
		m_boss.SendOutputFinished();
	} else
		throw fmt("Not implemented: %s", cmd.c_str());
}

void AirOS::GetTerminal() {
	if (m_term)
		return;
	if (m_phost["proto-ssh"]["auth"].GetData() != "userpass")
		throw std::string("Must use proto-ssh with auth \"userpass\" for Calix AE ONT");
	m_term = new Terminal(PROTO_SSH, m_phost["hostname"].GetData(),
	m_phost["proto-ssh"], "[^#]+# ", "--MORE--");
	m_term->Execute("");
}
