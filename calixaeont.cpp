#include <cmath>
#include <sstream>

#include "host.hpp"
#include "terminal.hpp"

class CalixAEONT : public Host {
public:
	CalixAEONT(const Boss& boss, const PropTree& phost);
	virtual ~CalixAEONT();

	virtual void Execute(const std::string& cmd, const std::string& args);

private:
	void GetTerminal();

	Terminal* m_term;
};

static HostFactoryRegistrant< CalixAEONT > r("calixaeont");


struct ONTCommandCB : public DataCallback {
	const Boss& boss;
	ONTCommandCB(const Boss& b) :
		boss(b)
	{}
	virtual void OnData(const std::string& data) {
		if (data.substr(0, 6) == "failed")
			boss.SendError(data);
	}
};


CalixAEONT::CalixAEONT(const Boss& boss, const PropTree& phost)
	: Host(boss, phost),
	m_term(0) {
}
CalixAEONT::~CalixAEONT() {
	if (m_term)
		delete m_term;
}

void CalixAEONT::Execute(const std::string& cmd, const std::string& args) {
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

void CalixAEONT::GetTerminal() {
	if (m_term)
		return;
	if (m_phost["proto-telnet"]["auth"].GetData() != "userpass")
		throw std::string("Must use proto-telnet with auth \"userpass\" for Calix AE ONT");
	m_term = new Terminal(PROTO_TELNET, m_phost["hostname"].GetData(),
	m_phost["proto-telnet"], ".?Enter login name:", "--MORE--");
	m_term->SetPromptRegex("Enter password:");
	m_term->Execute(m_phost["proto-telnet"]["username"].GetData());
	m_term->SetPromptRegex("Enter <CR> to continue:");
	m_term->Execute(m_phost["proto-telnet"]["password"].GetData());
	m_term->SetPromptRegex("[^>]+> ");
	m_term->Execute("");
}
