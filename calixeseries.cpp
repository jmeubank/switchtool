#include <cmath>
#include <sstream>

#include "host.hpp"
#include "terminal.hpp"

class CalixESeries : public Host {
public:
	CalixESeries(const Boss& boss, const PropTree& phost);
	virtual ~CalixESeries();

	virtual void Execute(const std::string& cmd, const std::string& args);

private:
	void GetTerminal();

	Terminal* m_term;
};

static HostFactoryRegistrant< CalixESeries > r("calixeseries");


struct CalixCommandCB : public DataCallback {
	PropTree& result;
	CalixCommandCB(PropTree& r) :
		result(r)
	{}
	virtual void OnData(const std::string& data) {
		if (data.substr(0, 6) == "failed")
			result["errors"].ArrayPushBack(data);
	}
};


CalixESeries::CalixESeries(const Boss& boss, const PropTree& phost)
	: Host(boss, phost),
	m_term(0)
{
}
CalixESeries::~CalixESeries() {
	if (m_term)
		delete m_term;
}

void CalixESeries::Execute(const std::string& cmd, const std::string& args) {
	if (cmd == "list-ifaces") {
		PropTree ifaces_tree;
		GetTerminal();
		struct DCB1 : public DataCallback {
			pcrecpp::RE iface1;
			pcrecpp::RE speed1;
			pcrecpp::RE lag1;
			pcrecpp::RE lagspeed1;
			PropTree& iftree;
			PropTree* editing;
			DCB1(PropTree& t) :
			iface1("(([0-9]+\\/)*[gx][0-9]+)(.*)(trunk|edge|uplink|peerlink|downlink) *([^ ]+).*"),
			speed1("([0-9]+)(\\.[0-9]+)?(g|m)"),
			lag1("LAG Interface *: ([^(]+).*"),
			lagspeed1("  Current Rate *: ([0-9]*).*"),
			iftree(t),
			editing(0)
			{}
			virtual void OnData(const std::string& data) {
				std::string tid;
				std::string descr;
				std::string speed;
				if (iface1.FullMatch(data, &tid, (void*)0, &descr, (void*)0, &speed)) {
					editing = &(iftree[tid]);
					while (descr.length() > 0 && descr[0] == ' ')
						descr.erase(0, 1);
					while (descr.length() > 0
					&& (descr[descr.length() - 1] == ' ' || descr[descr.length() - 1] == '+'))
						descr.erase(descr.length() - 1, 1);
					(*editing)["description"].SetData(descr);
					int real_speed = 0;
					char speed_suffix;
					if (speed1.FullMatch(speed, &real_speed, (void*)0, &speed_suffix)) {
						if (speed_suffix == 'g')
							(*editing)["speed"].SetData(fmt("%d", real_speed * 1000));
						else
							(*editing)["speed"].SetData(fmt("%d", real_speed));
					} else
						(*editing)["speed"].SetData("0");
					(*editing)["members"];
					(*editing)["combiner"];
				} else if (lag1.FullMatch(data, &tid)) {
					while (tid.length() > 0 && tid[tid.length() - 1] == ' ')
						tid.erase(tid.length() - 1, 1);
					editing = &(iftree[tid]);
					(*editing)["description"].SetData(tid);
				} else if (lagspeed1.FullMatch(data, &speed)) {
					int real_speed = atoi(speed.c_str());
					int lag_ct = 0;
					if (real_speed > 0) {
						int base_speed = pow(10, floor(log10(real_speed)));
						lag_ct = real_speed / base_speed;
						real_speed = base_speed;
					}
					if (editing) {
						(*editing)["speed"].SetData(fmt("%d", real_speed * 1000));
						(*editing)["members"].SetData(fmt("%d", lag_ct));
						(*editing)["combiner"];
					}
				}
			}
		} dcb1(ifaces_tree);
		m_term->Execute("show interface", &dcb1);
		m_term->Execute("show interface lag detail", &dcb1);
		m_boss.SendPropTree("interfaces", ifaces_tree);
	} else if (cmd == "list-iface-details") {
		if (args.length() <= 0)
			throw std::string("Must provide a port to show details for");
		PropTree ifdata;
		ifdata["sfp-present"].SetData("0");
		GetTerminal();
		struct DCB3 : public DataCallback {
			PropTree& dtree;
			PropTree* editing;
			std::string line_combine;
			pcrecpp::RE continuation1;
			pcrecpp::RE mac1;
			pcrecpp::RE sfppresent1;
			pcrecpp::RE connectortype1;
			pcrecpp::RE sfpvendor1;
			pcrecpp::RE sfpversion1;
			pcrecpp::RE distancerating1;
			pcrecpp::RE txwave1;
			pcrecpp::RE lasertemp1;
			pcrecpp::RE txdbm1;
			pcrecpp::RE rxdbm1;
			DCB3(PropTree& t)
			 : dtree(t),
			 editing(0),
			 continuation1(" +(.*)"),
			 mac1("MAC address *: (.*)"),
			 sfppresent1("SFP *: .*present.*"),
			 connectortype1("Connector type *: (.*)"),
			 sfpvendor1("Vendor info *: (.*)"),
			 sfpversion1("Version info *: (.*)"),
			 distancerating1("Link length *: (.*)"),
			 txwave1("Wavelength *: ([0-9]+(\\.[0-9]+)?).*"),
			 lasertemp1(".*Temp: (.*)"),
			 txdbm1(".*TX power: ([0-9]+)\\.([0-9]+)mW.*"),
			 rxdbm1(".*RX power: ([0-9]+)\\.([0-9]+)mW.*")
			{}
			virtual void OnData(const std::string& data) {
				std::string val;
				int ival1;
				int ival2;
				if (line_combine.length() > 0) {
					if (continuation1.FullMatch(data, &val)) {
						line_combine += std::string(" ") + val;
						return;
					}
					editing->SetData(line_combine);
					line_combine.clear();
				}
				if (mac1.FullMatch(data, &val))
					dtree["iface-mac"] = val;
				else if (sfppresent1.FullMatch(data))
					dtree["sfp-present"] = "1";
				else if (connectortype1.FullMatch(data, &val)) {
					editing = &(dtree["connector-type"]);
					line_combine = val;
				} else if (sfpvendor1.FullMatch(data, &val)) {
					editing = &(dtree["sfp-vendor"]);
					line_combine = val;
				} else if (sfpversion1.FullMatch(data, &val)) {
					editing = &(dtree["sfp-version"]);
					line_combine = val;
				} else if (distancerating1.FullMatch(data, &val)) {
					editing = &(dtree["distance-rating"]);
					line_combine = val;
				} else if (txwave1.FullMatch(data, &val))
					dtree["tx-wave"] =  val + "nm";
				else if (lasertemp1.FullMatch(data, &val))
					dtree["laser-temp"] = val;
				else if (txdbm1.FullMatch(data, &ival1, &ival2)) {
					ival1 *= 10000;
					if (ival2 >= 1000)
						ival1 += ival2;
					else if (ival2 >= 100)
						ival1 += (ival2 * 10);
					else if (ival2 >= 10)
						ival1 += (ival2 * 100);
					else
						ival1 += (ival2 * 1000);
					if (ival1 >= 65535)
						dtree["tx-dbm"] = "inf";
					else {
						dtree["tx-dbm"] =
						fmt("%.2f", 10.0f * log10((float)ival1) - 40.0f);
					}
				} else if (rxdbm1.FullMatch(data, &ival1, &ival2)) {
					ival1 *= 10000;
					if (ival2 >= 1000)
						ival1 += ival2;
					else if (ival2 >= 100)
						ival1 += (ival2 * 10);
					else if (ival2 >= 10)
						ival1 += (ival2 * 100);
					else
						ival1 += (ival2 * 1000);
					if (ival1 >= 65535)
						dtree["rx-dbm"] = "inf";
					else {
						dtree["rx-dbm"] =
						fmt("%.2f", 10.0f * log10((float)ival1) - 40.0f);
					}
				}
			}
		} dcb3(ifdata);
		m_term->Execute(std::string("show eth-port ") + args + " detail", &dcb3);
		m_boss.SendPropTree("iface-details", ifdata);
	} else if (cmd == "get-vlan-info") {
		if (args.length() <= 0)
			throw std::string("Must provide a VLAN to show");
		if (!pcrecpp::RE("[0-9]{1,4}").FullMatch(args))
			throw fmt("Invalid vlan ID: %s", args.c_str());
		PropTree vlan_info;
		struct DCB2 : public DataCallback {
			PropTree& vinfo;
			pcrecpp::RE vname1;
			pcrecpp::RE vmem1;
			DCB2(PropTree& v) :
				vinfo(v),
				vname1("[0-9]{1,4} \"([^\"]+)\" *(enabled|disabled|snoop-suppress|proxy|flood).*"),
				vmem1("[0-9]{1,4} *(.*)(Ethernet|LAG|EAPS|ERPS).*membership.*")
			{}
			virtual void OnData(const std::string& data) {
				std::string val;
				if (vname1.FullMatch(data, &val))
					vinfo["name"] = val;
				else if (vmem1.FullMatch(data, &val)) {
					while (val[val.length() - 1] == ' ')
						val.erase(val.length() - 1, 1);
					vinfo["interfaces"].ArrayPushBack(val);
				}
			}
		} dcb2(vlan_info);
		GetTerminal();
		m_term->Execute(std::string("show vlan ") + args, &dcb2);
		m_term->Execute(std::string("show vlan ") + args + " members", &dcb2);
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
		CalixCommandCB ccb(result);
		while (true) {
			if (create1.Consume(&input, &vlan_id, &vlan_name)) {
				m_term->Execute(std::string("create vlan ") + vlan_id
				+ " name \"" + vlan_name + "\"", &ccb);
			} else if (rename1.Consume(&input, &vlan_id, &vlan_name)) {
				m_term->Execute(std::string("set vlan ") + vlan_id
				+ " name \"" + vlan_name + "\"", &ccb);
			} else if (addmembers1.Consume(&input, &vlan_id)) {
				std::string iftid;
				while (iface1.Consume(&input, &iftid)) {
					m_term->Execute(std::string("add interface \"") + iftid
					+ "\" to-vlan " + vlan_id, &ccb);
				}
			} else if (removemembers1.Consume(&input, &vlan_id)) {
				std::string iftid;
				while (iface1.Consume(&input, &iftid)) {
					m_term->Execute(std::string("remove interface \"") + iftid
					+ "\" from-vlan " + vlan_id, &ccb);
				}
			} else if (delete1.Consume(&input, &vlan_id))
				m_term->Execute(std::string("delete vlan ") + vlan_id, &ccb);
			else
				break;
		}
		if (!result.ChildExists("errors"))
			result["success"] = "1";
		m_boss.SendPropTree("result", result);
	} else if (cmd == "get-half-duplex-ifaces") {
		GetTerminal();
		PropTree ifaces_send;
		struct DCB4 : public DataCallback {
			pcrecpp::RE iface1;
			pcrecpp::RE speed1;
			pcrecpp::RE opstate1;
			pcrecpp::RE opstatedown1;
			pcrecpp::RE currentstate1;
			std::string unverified_iface;
			PropTree& to_send;
			DCB4(PropTree& send) :
			iface1("(.*([0-9]+\\/)*[gx][0-9]+[^:]*).*"),
			speed1("Speed *: ([a-z0-9]+).*"),
			opstate1("Operational status *: .*disabled.*"),
			opstatedown1(".*disabled.*"),
			currentstate1("Current port state *: ((.*full-duplex)|(N/A)).*"),
			to_send(send)
			{}
			virtual void OnData(const std::string& data) {
				std::string new_iface;
				if (iface1.FullMatch(data, &new_iface)) {
					if (unverified_iface.length() > 0)
						to_send.ArrayPushBack(unverified_iface);
					unverified_iface = new_iface;
					return;
				}
				if (unverified_iface.length() <= 0)
					return;
				std::string val;
				if (speed1.FullMatch(data, &val)) {
					if (val != "auto")
						unverified_iface.clear();
					return;
				}
				if (opstate1.FullMatch(data)) {
					unverified_iface.clear();
					return;
				}
				if (currentstate1.FullMatch(data)) {
					unverified_iface.clear();
					return;
				}
			}
		} dcb4(ifaces_send);
		m_term->Execute("show eth-port detail", &dcb4);
		m_term->Execute("show ont-port detail", &dcb4);
		m_boss.SendPropTree("interfaces", ifaces_send);
	} else
		throw fmt("Not implemented: %s", cmd.c_str());
}

void CalixESeries::GetTerminal() {
	if (m_term)
		return;
	if (m_phost.ChildExists("proto-ssh")) {
		m_term = new Terminal(PROTO_SSH, m_phost["hostname"],
		m_phost["proto-ssh"], "[a-zA-Z0-9_-]+>", "--MORE--");
	} else {
		if (!m_phost.ChildExists("proto-telnet"))
			throw std::string("Must use proto-ssh or proto-telnet for Calix E-series");
		m_term = new Terminal(PROTO_TELNET, m_phost["hostname"],
		m_phost["proto-telnet"], ".?Username: ", "--MORE--");
		m_term->SetPromptRegex("Password: ");
		m_term->Execute(m_phost["auth-userpass"]["username"]);
		m_term->SetPromptRegex("[a-zA-Z0-9_-]+>");
		m_term->Execute(m_phost["auth-userpass"]["password"]);
	}
}
