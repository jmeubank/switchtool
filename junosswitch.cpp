#include <sstream>
#include <vector>
#include <list>
#include <cmath>

#include "host.hpp"
#include "terminal.hpp"
#include "snmp.hpp"
#include "tinyxml/tinyxml.h"


class JunosSwitch : public Host {
public:
	typedef std::map< std::string, std::pair< std::string, std::list< std::string > > > VlanDBMap;
	typedef std::map< std::string, std::list< std::string > > CombinerDBMap;
	typedef std::map< std::string, std::string > IfaceCombinerMap;

	JunosSwitch(const Boss& boss, const PropTree& phost);
	virtual ~JunosSwitch();

	virtual void Execute(const std::string& cmd, const std::string& args);

private:
	void GetTerminal();
	void LoadDB();
	void LoadCombinerDB();
	void LockConfig();
	void CommitConfig(PropTree* result);

	Terminal* m_term;
	VlanDBMap* m_vlandb;
	CombinerDBMap* m_combinerdb;
	IfaceCombinerMap* m_ifacecombinerdb;
	TiXmlDocument* m_config;
};

static HostFactoryRegistrant< JunosSwitch > r("junosswitch");


struct JunosCommandCB : public DataCallback {
	const Boss& boss;
	bool abort_on_fail;
	JunosCommandCB(const Boss& b) :
		boss(b),
		abort_on_fail(false)
	{}
	virtual void OnData(const std::string& data) {
		TiXmlDocument doc;
		doc.Parse(data.c_str());
		if (doc.Error()) {
			if (abort_on_fail)
				throw fmt("XML error: %s", doc.ErrorDesc());
			else
				boss.SendError(fmt("XML error: %s", doc.ErrorDesc()));
		}
		if (!doc.RootElement()->FirstChildElement("ok")) {
			printf("---\n%s\n---\n", data.c_str());
			TiXmlElement* p = doc.RootElement();
			TiXmlElement* c = p->FirstChildElement("commit-results");
			if (c)
				p = c;
			TiXmlText* emsg = TiXmlHandle(
				p->FirstChildElement("rpc-error")
			).FirstChildElement("error-message").FirstChild().ToText();
			if (abort_on_fail)
				throw std::string(emsg ? emsg->Value() : "Command failed for an unknown reason");
			else
				boss.SendError(emsg ? emsg->Value() : "Command failed for an unknown reason");
		}
	}
};


JunosSwitch::JunosSwitch(const Boss& boss, const PropTree& phost)
	: Host(boss, phost),
	m_term(0),
	m_vlandb(0),
	m_combinerdb(0),
	m_ifacecombinerdb(0),
	m_config(0)
{
}
JunosSwitch::~JunosSwitch() {
	CommitConfig(0);
	delete m_term;
	delete m_vlandb;
	delete m_combinerdb;
	delete m_ifacecombinerdb;
}

typedef std::map< std::string, std::string > IfaceMap;
void JunosSwitch::Execute(const std::string& cmd, const std::string& args) {
	if (cmd == "list-ifaces") {
		GetTerminal();
		LoadCombinerDB();
		PropTree ifaces_tree;
		struct DCB3 : public DataCallback {
			const Boss& boss;
			PropTree& iftree;
			IfaceCombinerMap& combiner_map;
			pcrecpp::RE iface1;
			pcrecpp::RE speed1;
			pcrecpp::RE speed2;
			pcrecpp::RE speed3;
			pcrecpp::RE ifaceup1;
			DCB3(const Boss& b, PropTree& t, IfaceCombinerMap& m) :
				boss(b),
				iftree(t),
				combiner_map(m),
				iface1("((ge|xe)-[0-9]+\\/[0-9]+(\\/[0-9]+)?)|(ae[0-9]+).*"),
				speed1("([0-9]+)m.*"),
				speed2("([0-9]+) Mbps.*"),
				speed3("([0-9]+)([MGT])bps.*"),
				ifaceup1("up.*")
			{}
			virtual void OnData(const std::string& data) {
				TiXmlDocument doc;
				doc.Parse(data.c_str());
				if (doc.Error())
					throw fmt("XML error: %s", doc.ErrorDesc());
				TiXmlElement* p = TiXmlHandle(
					doc.RootElement()->FirstChildElement("interface-information")
				).FirstChildElement("physical-interface").ToElement();
				if (!p) {
					TiXmlText* emsg = TiXmlHandle(
						doc.RootElement()->FirstChildElement("rpc-error")
					).FirstChildElement("error-message").FirstChild().ToText();
					if (emsg)
						throw fmt("RPC error: %s", emsg->Value());
					else
						throw std::string("RPC error: No interface information returned");
				}
				for (; p; p = p->NextSiblingElement("physical-interface")) {
					bool is_lag = false;
					TiXmlText* iname = TiXmlHandle(
						p->FirstChildElement("name")
					).FirstChild().ToText();
					if (!iname)
						continue;
					if (!iface1.FullMatch(iname->Value()))
						continue;
					if (iname->Value()[0] == 'a' && iname->Value()[1] == 'e')
						is_lag = true;
					PropTree& editing = iftree[iname->Value()];
					TiXmlText* idescr = TiXmlHandle(
						p->FirstChildElement("description")
					).FirstChild().ToText();
					if (idescr)
						editing["description"] = idescr->Value();
					else
						editing["description"];
					int speed_i = -1;
					char mult_char = 'M';
					TiXmlText* ioper = TiXmlHandle(
						p->FirstChildElement("oper-status")
					).FirstChild().ToText();
					if (ioper && ifaceup1.FullMatch(ioper->Value())) {
						TiXmlText* ispeed = TiXmlHandle(
							p->FirstChildElement("speed")
						).FirstChild().ToText();
						if (ispeed) {
							if (!speed1.FullMatch(ispeed->Value(), &speed_i)) {
								speed3.FullMatch(ispeed->Value(), &speed_i, &mult_char);
							}
						}
						if (speed_i < 0) {
							TiXmlText* lpspeed = TiXmlHandle(
								p->FirstChildElement("ethernet-autonegotiation")
							).FirstChildElement("link-partner-speed").FirstChild()
							.ToText();
							if (!lpspeed
							|| !speed2.FullMatch(lpspeed->Value(), &speed_i))
								speed_i = 10;
						}
					}
					int speed_multiplier = 1;
					if (speed_i < 0)
						speed_i = 0;
					else if (mult_char == 'G')
						speed_multiplier = 1000;
					else if (mult_char == 'T')
						speed_multiplier = 1000000;
					speed_i *= speed_multiplier;
					if (is_lag) {
						if (speed_i > 0) {
							int dec_size = (int)pow(10, (int)log10(speed_i));
							editing["members"] = dynamic_cast< std::ostringstream& >(
								std::ostringstream() << std::dec
								<< (speed_i / dec_size)
							).str();
							speed_i /= (speed_i / dec_size);
						}
						else
							editing["members"] = "0";
					} else
						editing["members"];
					editing["speed"] = dynamic_cast< std::ostringstream& >(
						(std::ostringstream() << std::dec << speed_i)
					).str();
					IfaceCombinerMap::const_iterator fd
					= combiner_map.find(iname->Value());
					if (fd == combiner_map.end())
						editing["combiner"];
					else
						editing["combiner"] = fd->second;
				}
			}
		} dcb3(m_boss, ifaces_tree, *m_ifacecombinerdb);
		m_term->Execute("<rpc><get-interface-information><extensive/></get-interface-information></rpc>", &dcb3);
		m_boss.SendPropTree("interfaces", ifaces_tree);
	} else if (cmd == "list-ifaces-old") {
		std::string community = m_phost["auth-snmp2"];
		if (community.length() <= 0)
			throw fmt("Must supply an SNMPv2 community string for JunOS list-ifaces-old");
		std::string ip = m_phost["hostname"];
		if (ip.length() <= 0)
			throw fmt("Must supply a hostname or IP address for JunOS list-ifaces-old");
		PropTree ifaces_tree;
		struct SCB1 : public SNMPCallback {
			PropTree iftree;
			IfaceMap ifaces;
			int step;
			pcrecpp::RE iface1;
			SCB1(PropTree& t)
			 : iftree(t),
			 iface1("(ge|xe)-[0-9]+\\/[0-9]+(\\/[0-9]+)?")
			{}
			virtual void OnData(const std::string& num, const std::string& val) {
				if (step == 0) {
					if (iface1.FullMatch(val.substr(8)))
						ifaces[num] = val.substr(8);
				} else if (step == 1) {
					IfaceMap::iterator fd = ifaces.find(num);
					if (fd != ifaces.end())
						iftree[fd->second]["description"] = val.substr(8);
				} else if (step == 2) {
					IfaceMap::iterator fd = ifaces.find(num);
					if (fd != ifaces.end())
						iftree[fd->second]["speed"] = val.substr(9);
				} else if (step == 3) {
					IfaceMap::iterator fd = ifaces.find(num);
					if (fd != ifaces.end() && val.substr(9, 2) != "up")
						iftree[fd->second]["speed"] = "0";
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
		m_boss.SendPropTree("interfaces", ifaces_tree);
	} else if (cmd == "get-vlan-info") {
		if (args.length() <= 0)
			throw std::string("Must provide a VLAN to show");
		if (!pcrecpp::RE("[0-9]{1,4}").FullMatch(args))
			throw fmt("Invalid vlan ID: %s", args.c_str());
		LoadDB();
		PropTree vlan_data;
		VlanDBMap::const_iterator fd = m_vlandb->find(args);
		if (fd != m_vlandb->end()) {
			std::string vname = fd->second.first;
			pcrecpp::RE("V[0-9]{1,4}-(.*)").FullMatch(fd->second.first, &vname);
			vlan_data["name"] = vname;
			for (std::list< std::string >::const_iterator it
			= fd->second.second.begin();
			it != fd->second.second.end(); ++it) {
				vlan_data["interfaces"].ArrayPushBack(*it);
			}
		}
		m_boss.SendPropTree("vlan", vlan_data);
	} else if (cmd == "mod-vlans") {
		PropTree result;
		LoadDB();
		LockConfig();
		pcrecpp::StringPiece input(args);
		pcrecpp::RE create1("create ([0-9]{1,4}) \"([a-zA-Z0-9_-]+)\" *");
		pcrecpp::RE rename1("rename ([0-9]{1,4}) \"([a-zA-Z0-9_-]+)\" *");
		pcrecpp::RE addmembers1("add-members ([0-9]{1,4}) ");
		pcrecpp::RE removemembers1("remove-members ([0-9]{1,4}) ");
		pcrecpp::RE delete1("delete ([0-9]{1,4}) *");
		pcrecpp::RE iface1("iface:\"([^\"]+)\" *");
		pcrecpp::RE vname1("V[0-9]{1,4}-.*");
		std::string vlan_id;
		std::string vlan_name;
		while (true) {
			if (create1.Consume(&input, &vlan_id, &vlan_name)) {
				if (!vname1.FullMatch(vlan_name))
					vlan_name = std::string("V") + vlan_id + "-" + vlan_name;
				TiXmlElement* v = new TiXmlElement("vlan");
				TiXmlElement* n = new TiXmlElement("name");
				TiXmlText* nt = new TiXmlText(vlan_name);
				n->LinkEndChild(nt);
				v->LinkEndChild(n);
				TiXmlElement* i = new TiXmlElement("vlan-id");
				TiXmlText* it = new TiXmlText(vlan_id);
				i->LinkEndChild(it);
				v->LinkEndChild(i);
				m_config->RootElement()->FirstChildElement("edit-config")
				->FirstChildElement("config")->FirstChildElement("configuration")
				->FirstChildElement("vlans")->LinkEndChild(v);
				m_vlandb->insert(std::make_pair(vlan_id, std::make_pair(
					vlan_name,
					std::list< std::string >()
				)));
			} else if (rename1.Consume(&input, &vlan_id, &vlan_name)) {
			} else if (addmembers1.Consume(&input, &vlan_id)) {
				VlanDBMap::const_iterator fd = m_vlandb->find(vlan_id);
				if (fd != m_vlandb->end()) {
					TiXmlElement* v = m_config->RootElement()
					->FirstChildElement("edit-config")
					->FirstChildElement("config")
					->FirstChildElement("configuration")
					->FirstChildElement("vlans")->FirstChildElement("vlan");
					for (; v; v = v->NextSiblingElement("vlan")) {
						if (fd->second.first ==
						v->FirstChildElement("name")->FirstChild()->ToText()->Value())
							break;
					}
					if (!v) {
						v = new TiXmlElement("vlan");
						TiXmlElement* n = new TiXmlElement("name");
						TiXmlText* nt = new TiXmlText(fd->second.first);
						n->LinkEndChild(nt);
						v->LinkEndChild(n);
						m_config->RootElement()->FirstChildElement("edit-config")
						->FirstChildElement("config")->FirstChildElement("configuration")
						->FirstChildElement("vlans")->LinkEndChild(v);
					}
					std::string iftid;
					while (iface1.Consume(&input, &iftid)) {
						TiXmlElement* i = new TiXmlElement("interface");
						TiXmlElement* n = new TiXmlElement("name");
						TiXmlText* nt = new TiXmlText(iftid + ".0");
						n->LinkEndChild(nt);
						i->LinkEndChild(n);
						v->LinkEndChild(i);
					}
				} else {
					result["errors"].ArrayPushBack(
						std::string("VLAN ") + vlan_id + " not present"
					);
				}
			} else if (removemembers1.Consume(&input, &vlan_id)) {
				VlanDBMap::const_iterator fd = m_vlandb->find(vlan_id);
				if (fd != m_vlandb->end()) {
					TiXmlElement* v = m_config->RootElement()
					->FirstChildElement("edit-config")
					->FirstChildElement("config")
					->FirstChildElement("configuration")
					->FirstChildElement("vlans")->FirstChildElement("vlan");
					for (; v; v = v->NextSiblingElement("vlan")) {
						if (fd->second.first ==
						v->FirstChildElement("name")->FirstChild()->ToText()->Value())
							break;
					}
					if (!v) {
						v = new TiXmlElement("vlan");
						TiXmlElement* n = new TiXmlElement("name");
						TiXmlText* nt = new TiXmlText(fd->second.first);
						n->LinkEndChild(nt);
						v->LinkEndChild(n);
						m_config->RootElement()->FirstChildElement("edit-config")
						->FirstChildElement("config")->FirstChildElement("configuration")
						->FirstChildElement("vlans")->LinkEndChild(v);
					}
					std::string iftid;
					while (iface1.Consume(&input, &iftid)) {
						TiXmlElement* i = new TiXmlElement("interface");
						i->SetAttribute("operation", "delete");
						TiXmlElement* n = new TiXmlElement("name");
						TiXmlText* nt = new TiXmlText(iftid + ".0");
						n->LinkEndChild(nt);
						i->LinkEndChild(n);
						v->LinkEndChild(i);
					}
				} else {
					result["errors"].ArrayPushBack(
						std::string("VLAN ") + vlan_id + " not present"
					);
				}
			} else if (delete1.Consume(&input, &vlan_id)) {
				VlanDBMap::iterator fd = m_vlandb->find(vlan_id);
				if (fd != m_vlandb->end()) {
					TiXmlElement* v = new TiXmlElement("vlan");
					v->SetAttribute("operation", "delete");
					TiXmlElement* n = new TiXmlElement("name");
					TiXmlText* nt = new TiXmlText(fd->second.first);
					n->LinkEndChild(nt);
					v->LinkEndChild(n);
					m_config->RootElement()->FirstChildElement("edit-config")
					->FirstChildElement("config")->FirstChildElement("configuration")
					->FirstChildElement("vlans")->LinkEndChild(v);
					m_vlandb->erase(fd);
				} else {
					result["errors"].ArrayPushBack(
						std::string("VLAN ") + vlan_id + " not present"
					);
				}
			} else
				break;
		}
		CommitConfig(&result);
		if (!result.ChildExists("errors"))
			result["success"] = "1";
		m_boss.SendPropTree("result", result);
	} else if (cmd == "get-half-duplex-ifaces") {
		GetTerminal();
		struct DCB3 : public DataCallback {
			const Boss& boss;
			pcrecpp::RE iface1;
			DCB3(const Boss& b) :
			 boss(b),
			 iface1("((ge|xe)-[0-9]+\\/[0-9]+(\\/[0-9]+)?).*")
			{}
			virtual void OnData(const std::string& data) {
				TiXmlDocument doc;
				doc.Parse(data.c_str());
				if (doc.Error())
					throw fmt("XML error: %s", doc.ErrorDesc());
				TiXmlElement* p = TiXmlHandle(
					doc.RootElement()->FirstChildElement("interface-information")
				).FirstChildElement("physical-interface").ToElement();
				if (!p) {
					TiXmlText* emsg = TiXmlHandle(
						doc.RootElement()->FirstChildElement("rpc-error")
					).FirstChildElement("error-message").FirstChild().ToText();
					if (emsg)
						throw fmt("RPC error: %s", emsg->Value());
					else
						throw std::string("RPC error: No interface information returned");
				}
				PropTree hdifaces;
				for (; p; p = p->NextSiblingElement("physical-interface")) {
					TiXmlText* iname = TiXmlHandle(
						p->FirstChildElement("name")
					).FirstChild().ToText();
					if (!iname || !iface1.FullMatch(iname->Value()))
						continue;
					TiXmlText* ostatus = TiXmlHandle(
						p->FirstChildElement("oper-status")
					).FirstChild().ToText();
					if (!ostatus || strcmp(ostatus->Value(), "up") != 0)
						continue;
					TiXmlText* duplex = TiXmlHandle(
						p->FirstChildElement("duplex")
					).FirstChild().ToText();
					if (!duplex || strcmp(duplex->Value(), "Auto") != 0)
						continue;
					TiXmlText* lpd = TiXmlHandle(
						p->FirstChildElement("ethernet-autonegotiation")
					).FirstChildElement("link-partner-duplexity").FirstChild()
					.ToText();
					if (!lpd || strcmp(lpd->Value(), "full-duplex") == 0)
						continue;
					hdifaces.ArrayPushBack(std::string(iname->Value()));
				}
				boss.SendPropTree("interfaces", hdifaces);
			}
		} dcb5(m_boss);
		m_term->Execute("<rpc><get-interface-information><extensive/></get-interface-information></rpc>", &dcb5);
	} else
		throw fmt("Not implemented: %s", cmd.c_str());
}

void JunosSwitch::GetTerminal() {
	if (m_term)
		return;
	if (!m_phost.ChildExists("proto-netconfssh"))
		throw fmt("Must use proto-netconfssh for a JunOS switch");
	m_term = new Terminal(PROTO_NETCONF_SSH, m_phost["hostname"],
	m_phost["proto-netconfssh"]);
	m_term->Execute(
"<hello> \
  <capabilities> \
    <capability>urn:ietf:params:xml:ns:netconf:base:1.0</capability> \
    <capability>urn:ietf:params:xml:ns:netconf:capability:candidate:1.0</capability> \
    <capability>urn:ietf:params:xml:ns:netconf:capability:confirmed-commit:1.0</capability> \
    <capability>urn:ietf:params:xml:ns:netconf:capability:validate:1.0</capability> \
    <capability>urn:ietf:params:xml:ns:netconf:capability:url:1.0?protocol=http,ftp,file</capability> \
    <capability>http://xml.juniper.net/netconf/junos/1.0</capability> \
    <capability>http://xml.juniper.net/dmi/system/1.0</capability> \
  </capabilities> \
</hello>"
	);
}

void JunosSwitch::LoadDB() {
	LoadCombinerDB();
	if (m_vlandb)
		return;
	m_vlandb = new VlanDBMap;
	GetTerminal();
	struct DCB2 : public DataCallback {
		VlanDBMap& vlans;
		pcrecpp::RE iface1;
		DCB2(VlanDBMap& v) :
			vlans(v),
			iface1("(((ge|xe)-[0-9]+\\/[0-9]+(\\/[0-9]+)?)|(ae[0-9]+)).*")
		{}
		virtual void OnData(const std::string& data) {
			TiXmlDocument doc;
			doc.Parse(data.c_str());
			if (doc.Error())
				throw fmt("XML error: %s", doc.ErrorDesc());
			TiXmlElement* v = TiXmlHandle(
				doc.RootElement()->FirstChildElement("vlan-information")
			).FirstChildElement("vlan").ToElement();
			if (!v) {
				TiXmlText* emsg = TiXmlHandle(
					doc.RootElement()->FirstChildElement("rpc-error")
				).FirstChildElement("error-message").FirstChild().ToText();
				if (emsg)
					throw fmt("RPC error: %s", emsg->Value());
				else
					throw std::string("RPC error: No vlan information returned");
			}
			for (; v; v = v->NextSiblingElement("vlan")) {
				TiXmlText* vtag = TiXmlHandle(
					v->FirstChildElement("vlan-tag")
				).FirstChild().ToText();
				if (!vtag)
					continue;
				TiXmlText* vnt = TiXmlHandle(
					v->FirstChildElement("vlan-name")
				).FirstChild().ToText();
				std::string vname;
				if (vnt)
					vname = vnt->Value();
				std::list< std::string >& iflist = vlans.insert(
					std::make_pair(
						std::string(vtag->Value()),
						std::make_pair(vname, std::list< std::string >())
					)
				).first->second.second;
				for (TiXmlElement* m = TiXmlHandle(
					v->FirstChildElement("vlan-detail")
				).FirstChildElement("vlan-member-list")
				.FirstChildElement("vlan-member").ToElement();
				m; m = m->NextSiblingElement("vlan-member")) {
					TiXmlText* iface = TiXmlHandle(
						m->FirstChildElement("vlan-member-interface")
					).FirstChild().ToText();
					if (iface) {
						std::string real_iface;
						if (iface1.FullMatch(iface->Value(), &real_iface))
							iflist.push_back(real_iface);
					}
				}
			}
		}
	} dcb2(*m_vlandb);
	m_term->Execute("<rpc><get-vlan-information/></rpc>", &dcb2);
}

void JunosSwitch::LoadCombinerDB() {
	if (m_combinerdb)
		return;
	m_combinerdb = new CombinerDBMap;
	m_ifacecombinerdb = new IfaceCombinerMap;
	return;
	GetTerminal();
	struct DCB5 : public DataCallback {
		Boss const& boss;
		CombinerDBMap& combiners;
		IfaceCombinerMap& iface_combiners;
		pcrecpp::RE iface1;
		DCB5(Boss const& b, CombinerDBMap& c, IfaceCombinerMap& i) :
			boss(b),
			combiners(c),
			iface_combiners(i),
			iface1("(((ge|xe)-[0-9]+\\/[0-9]+(\\/[0-9]+)?)|(ae[0-9]+)).*")
		{}
		virtual void OnData(const std::string& data) {
			TiXmlDocument doc;
			doc.Parse(data.c_str());
			if (doc.Error())
				throw fmt("XML error: %s", doc.ErrorDesc());
			TiXmlElement* g = TiXmlHandle(
				doc.RootElement()->FirstChildElement("erp-pg-configuration")
			).FirstChildElement("erp-protection-group").ToElement();
			if (!g)
				return;
			for (; g; g = g->NextSiblingElement("erp-protection-group")) {
				TiXmlText* pgname = TiXmlHandle(
					g->FirstChildElement("erp-pg-name")
				).FirstChild().ToText();
				if (!pgname || strlen(pgname->Value()) <= 0)
					continue;
				std::list< std::string >& iflist = combiners.insert(
					std::make_pair(
						std::string(pgname->Value()), std::list< std::string >()
					)
				).first->second;
				TiXmlText* xifname = TiXmlHandle(
					g->FirstChildElement("erp-pg-east-interface-name")
				).FirstChild().ToText();
				std::string ifname;
				if (iface1.FullMatch(xifname->Value(), &ifname)) {
					iflist.push_back(ifname);
					iface_combiners[ifname] = std::string("erp:")
					+ pgname->Value();
					printf("%s\n", pgname->Value());
				}
				xifname = TiXmlHandle(
					g->FirstChildElement("erp-pg-west-interface-name")
				).FirstChild().ToText();
				if (iface1.FullMatch(xifname->Value(), &ifname)) {
					iflist.push_back(ifname);
					iface_combiners[ifname] = std::string("erp:")
					+ pgname->Value();
				}
			}
		}
	} dcb5(m_boss, *m_combinerdb, *m_ifacecombinerdb);
	m_term->Execute("<rpc><get-ring-configuration/></rpc>", &dcb5);
}

void JunosSwitch::LockConfig() {
	if (m_config)
		return;
	GetTerminal();
	JunosCommandCB jcb(m_boss);
	jcb.abort_on_fail = true;
	m_term->Execute("<rpc><lock><target><candidate/></target></lock></rpc>", &jcb);
	m_config = new TiXmlDocument;
	m_config->Parse(
"<rpc> \
  <edit-config> \
    <target><candidate/></target> \
    <config> \
      <configuration> \
        <vlans/> \
      </configuration> \
    </config> \
  </edit-config> \
</rpc>"
	);
	if (m_config->Error())
		throw fmt("XML error: %s", m_config->ErrorDesc());
}

void JunosSwitch::CommitConfig(PropTree* result) {
	if (!m_config)
		return;
	delete m_vlandb;
	m_vlandb = 0;
	std::string newconfig;
	newconfig << (*m_config);
	GetTerminal();
	JunosCommandCB jcb(m_boss);
	jcb.abort_on_fail = true;
	try {
		m_term->Execute(newconfig, &jcb);
		m_term->Execute("<rpc><commit/></rpc>", &jcb);
	} catch(std::string& e) {
		if (result)
			(*result)["errors"].ArrayPushBack(e);
	}
	m_term->Execute("<rpc><unlock><target><candidate/></target></unlock></rpc>", &jcb);
	delete m_config;
	m_config = 0;
}
