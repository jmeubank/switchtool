#include <cmath>
#include <vector>
#include <sstream>

#include "host.hpp"
#include "terminal.hpp"
#include "snmp.hpp"


class CiscoIOS : public Host {
public:
	CiscoIOS(const Boss& boss, const PropTree& phost);
	virtual ~CiscoIOS();

	virtual void Execute(const std::string& cmd, const std::string& args);

private:
	static const char* REGEX_ROOT;
	static const char* REGEX_CONFIG;
	static const char* REGEX_CONFIG_IF;
	static const char* REGEX_CONFIG_VLAN;

	void GetTerminal();

	Terminal* m_term;
};

static HostFactoryRegistrant< CiscoIOS > r("ciscoios");


const char* CiscoIOS::REGEX_ROOT = "[a-zA-Z0-9_-]+\\#";
const char* CiscoIOS::REGEX_CONFIG = "[a-zA-Z0-9_-]+\\(config\\)\\#";
const char* CiscoIOS::REGEX_CONFIG_IF = "[a-zA-Z0-9_-]+\\(config-if\\)\\#";
const char* CiscoIOS::REGEX_CONFIG_VLAN = "[a-zA-Z0-9_-]+\\(config-vlan\\)\\#";


struct CiscoCommandCB : public DataCallback {
	PropTree& result;
	CiscoCommandCB(PropTree& r) :
		result(r)
	{}
	virtual void OnData(const std::string& data) {
		result["errors"].ArrayPushBack(data);
	}
};
struct WriteMemCommandCB : public DataCallback {
	PropTree& result;
	WriteMemCommandCB(PropTree& r) :
		result(r)
	{}
	virtual void OnData(const std::string& data) {
		if (data.substr(0, 25) != "Building configuration..."
		&& data.substr(0, 4) != "[OK]")
			result["errors"].ArrayPushBack(data);
	}
};


CiscoIOS::CiscoIOS(const Boss& boss, const PropTree& phost)
	: Host(boss, phost),
	m_term(0)
{
}
CiscoIOS::~CiscoIOS() {
	if (m_term)
		delete m_term;
}

typedef std::map< std::string, std::string > IfaceMap;
void CiscoIOS::Execute(const std::string& cmd, const std::string& args) {
	if (cmd == "list-ifaces") {
		std::string community = m_phost["proto-snmp2"];
		if (community.length() <= 0)
			throw fmt("Must supply an proto-snmp2 community string for Cisco IOS switch");
		std::string ip = m_phost["hostname"];
		if (ip.length() <= 0)
			throw fmt("Must supply a hostname or IP address for Cisco IOS switch");
		PropTree ifaces_tree;
		struct SCB1 : public SNMPCallback {
			PropTree& iftree;
			IfaceMap ifaces;
			int step;
			pcrecpp::RE iface1;
			SCB1(PropTree& t)
			 : iftree(t),
			 iface1("(Fa|Gi|Po)[0-9]+(\\/[0-9]+)*")
			{}
			virtual void OnData(const std::string& num,
			const std::string& val) {
				/* printf("%s\n", val.c_str()); */
				if (step == 0) {
					std::string ifname = SNMPUnSTRING(val);
					if (iface1.FullMatch(ifname))
						ifaces[num] = ifname;
					if (ifname[0] == 'P' && ifname[1] == 'o')
						iftree[ifname]["members"] = "0";
				} else if (step == 1) {
					IfaceMap::iterator fd = ifaces.find(num);
					if (fd != ifaces.end())
						iftree[fd->second]["description"] = SNMPUnSTRING(val);
				} else if (step == 2) {
					IfaceMap::iterator fd = ifaces.find(num);
					if (fd != ifaces.end()) {
						iftree[fd->second]["speed"] = val.substr(9);
						iftree[fd->second]["members"];
						iftree[fd->second]["combiner"];
					}
				} else if (step == 3) {
					IfaceMap::iterator fd = ifaces.find(num);
					if (fd != ifaces.end() && SNMPUnSTRING(val) != "up" && val[9] != '1')
						iftree[fd->second]["speed"] = "0";
				} else if (step == 4) {
					if (val.substr(9) != num && val.substr(9) != "0") {
						IfaceMap::iterator fd = ifaces.find(val.substr(9));
						if (fd != ifaces.end()) {
							int members = atoi(
								iftree[fd->second]["members"].GetData().c_str()
							);
							++members;
							iftree[fd->second]["members"]
							= dynamic_cast< std::ostringstream& >(
								std::ostringstream() << std::dec << members
							).str();
						}
					}
				}
			}
		} scb1(ifaces_tree);
		scb1.step = 0;
		SNMPWalk(2, community, ip, ".1.3.6.1.2.1.31.1.1.1.1", &scb1);
		scb1.step = 1;
		SNMPWalk(2, community, ip, ".1.3.6.1.2.1.31.1.1.1.18", &scb1);
		scb1.step = 2;
		SNMPWalk(2, community, ip, ".1.3.6.1.2.1.31.1.1.1.15", &scb1);
		scb1.step = 3;
		SNMPWalk(2, community, ip, ".1.3.6.1.2.1.2.2.1.8", &scb1);
		scb1.step = 4;
		SNMPWalk(2, community, ip, ".1.3.6.1.4.1.9.9.98.1.1.1.1.8", &scb1);
		for (
			PropTree::iterator it = ifaces_tree.Begin();
			it != ifaces_tree.End();
			++it
		) {
			int members = atoi((*it)["members"].GetData().c_str());
			if (members > 0) {
				int speed = atoi((*it)["speed"].GetData().c_str());
				if (speed > 0) {
					speed /= members;
					(*it)["speed"]
					= dynamic_cast< std::ostringstream& >(
						std::ostringstream() << std::dec << speed
					).str();
				}
			}
		}
		m_boss.SendPropTree("interfaces", ifaces_tree);
	} else if (cmd == "get-vlan-info") {
		if (args.length() <= 0)
			throw std::string("Must provide a VLAN to show");
		if (!pcrecpp::RE("[0-9]{1,4}").FullMatch(args))
			throw fmt("Invalid vlan ID: %s", args.c_str());
		GetTerminal();
		PropTree vlan_info;
		struct DCB2 : public DataCallback {
			PropTree& vinfo;
			pcrecpp::RE ifmember1;
			pcrecpp::RE vname1;
			DCB2(PropTree& v) :
				vinfo(v),
				ifmember1("((Gi|Fa|Po)[0-9]+(\\/[0-9]+)*)"),
				vname1("[0-9]{1,4} *(.*) active.*")
			{}
			virtual void OnData(const std::string& data) {
				std::string name;
				if (vname1.FullMatch(data, &name)) {
					while (name[name.length() - 1] == ' ')
						name.erase(name.length() - 1, 1);
					vinfo["name"] = name;
				}
				pcrecpp::StringPiece input(data);
				std::string iface;
				while (ifmember1.FindAndConsume(&input, &iface))
					vinfo["interfaces"].ArrayPushBack(iface);
			}
		} dcb2(vlan_info);
		m_term->Execute(std::string("show vlan id ") + args, &dcb2);
		m_boss.SendPropTree("vlan", vlan_info);
	} else if (cmd == "mod-vlans") {
		GetTerminal();
		pcrecpp::StringPiece input(args);
		pcrecpp::RE create1("create ([0-9]{1,4}) \"([a-zA-Z0-9_-]+)\" *");
		pcrecpp::RE rename1("rename ([0-9]{1,4}) \"([a-zA-Z0-9_-]+)\" *");
		pcrecpp::RE addmembers1("add-members ([0-9]{1,4}) ");
		pcrecpp::RE removemembers1("remove-members ([0-9]{1,4}) ");
		pcrecpp::RE delete1("delete ([0-9]{1,4}) *");
		pcrecpp::RE iface1("iface:\"([^\"]+)\" *");
		std::string vlan_id;
		std::string vlan_name;
		PropTree result;
		CiscoCommandCB ccb(result);
		while (true) {
			if (create1.Consume(&input, &vlan_id, &vlan_name)
			|| rename1.Consume(&input, &vlan_id, &vlan_name)) {
				m_term->SetPromptRegex(REGEX_CONFIG);
				m_term->Execute("configure terminal");
				m_term->SetPromptRegex(REGEX_CONFIG_VLAN);
				m_term->Execute(std::string("vlan ") + vlan_id, &ccb);
				m_term->Execute(std::string("name ") + vlan_name, &ccb);
				m_term->SetPromptRegex(REGEX_CONFIG);
				m_term->Execute("exit", &ccb);
				m_term->SetPromptRegex(REGEX_ROOT);
				m_term->Execute("exit", &ccb);
			} else if (addmembers1.Consume(&input, &vlan_id)) {
				m_term->SetPromptRegex(REGEX_CONFIG);
				m_term->Execute("configure terminal");
				m_term->SetPromptRegex(REGEX_CONFIG_IF);
				std::string iftid;
				while (iface1.Consume(&input, &iftid)) {
					m_term->Execute(std::string("interface ") + iftid, &ccb);
					m_term->Execute(
						std::string("switchport trunk allowed vlan add ")
						+ vlan_id,
						&ccb
					);
				}
				m_term->SetPromptRegex(REGEX_CONFIG);
				m_term->Execute("exit", &ccb);
				m_term->SetPromptRegex(REGEX_ROOT);
				m_term->Execute("exit", &ccb);
			} else if (removemembers1.Consume(&input, &vlan_id)) {
				m_term->SetPromptRegex(REGEX_CONFIG);
				m_term->Execute("configure terminal");
				m_term->SetPromptRegex(REGEX_CONFIG_IF);
				std::string iftid;
				while (iface1.Consume(&input, &iftid)) {
					m_term->Execute(std::string("interface ") + iftid, &ccb);
					m_term->Execute(
						std::string("switchport trunk allowed vlan remove ")
						+ vlan_id,
						&ccb
					);
				}
				m_term->SetPromptRegex(REGEX_CONFIG);
				m_term->Execute("exit", &ccb);
				m_term->SetPromptRegex(REGEX_ROOT);
				m_term->Execute("exit", &ccb);
			} else if (delete1.Consume(&input, &vlan_id)) {
				m_term->SetPromptRegex(REGEX_CONFIG);
				m_term->Execute("configure terminal");
				m_term->Execute(std::string("no vlan ") + vlan_id, &ccb);
				m_term->SetPromptRegex(REGEX_ROOT);
				m_term->Execute("exit", &ccb);
			} else
				break;
		}
		m_term->SetPromptRegex(REGEX_ROOT);
		WriteMemCommandCB wcb(result);
		m_term->Execute("write memory", &wcb);
		if (!result.ChildExists("errors"))
			result["success"] = "1";
		m_boss.SendPropTree("result", result);
	} else
		throw fmt("Not implemented: %s", cmd.c_str());
}

void CiscoIOS::GetTerminal() {
	if (m_term)
		return;
	if (m_phost.ChildExists("proto-ssh")) {
		m_term = new Terminal(PROTO_SSH, m_phost["hostname"],
		m_phost["proto-ssh"], "[a-zA-Z0-9_-]+>", " --More-- ");
	} else {
		if (!m_phost.ChildExists("proto-telnet"))
			throw fmt("Must use -proto ssh or -proto telnet for a Cisco IOS switch");
		if (m_phost["proto-telnet"]["auth"] != "console")
			throw std::string("Only \"console\" auth type is supported for proto-telnet on Cisco IOS");
		m_term = new Terminal(PROTO_TELNET, m_phost["hostname"],
		m_phost["proto-telnet"], ".?Password: ", " --More-- ");
		m_term->SetPromptRegex("[a-zA-Z0-9_-]+>");
		m_term->Execute(m_phost["proto-telnet"]["password"]);
	}
	std::string enable_secret = m_phost["proto-telnet"]["enable"];
	if (enable_secret.length() <= 0)
		throw std::string("Must use -enable <secret> for Cisco IOS");
	m_term->SetPromptRegex("Password: ");
	m_term->Execute("enable");
	m_term->SetPromptRegex(REGEX_ROOT);
	try {
		m_term->Execute(enable_secret);
	} catch (std::string&) {
		throw std::string("Timeout or invalid enable secret");
	}
}
