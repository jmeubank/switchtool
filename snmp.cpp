#include <cstdio>
#include <pcrecpp.h>

#include "common.hpp"
#include "snmp.hpp"


void SNMPWalk(int version, const std::string& community, const std::string& ip,
const std::string& oid, SNMPCallback* scb) {
	std::string rn = fmt(
		"snmpbulkwalk -v %dc -c %s %s %s 2>&1",
		version,
		community.c_str(),
		ip.c_str(),
		oid.c_str()
	);
	FILE* p = popen(rn.c_str(), "r");
	if (!p)
		throw fmt("Failed to execute '%s'", rn.c_str());
	pcrecpp::RE snmp1(".*\\.([0-9]+) \\= (.*)");
	std::string line;
	char c;
	std::string num;
	std::string val;
	std::string err;
	while (true) {
		c = fgetc(p);
		if (feof(p) || c == '\n') {
			if (snmp1.FullMatch(line, &num, &val)) {
				if (scb)
					scb->OnData(num, val);
			} else if (line.length() > 0 && err.length() <= 0)
				err = line;
			if (feof(p))
				break;
			line.clear();
			continue;
		}
		line += c;
	}
	int ret = pclose(p);
	if (ret != 0)
		throw err;
}

std::string SNMPUnSTRING(const std::string& value)
{
	std::string ret;
	if (value.substr(0, 8) == "STRING: ")
		ret = value.substr(9);
	if (ret[0] == '"')
		ret = ret.substr(1);
	if (ret[ret.length() - 1] == '"')
		ret = ret.substr(0, ret.length() - 1);
	return ret;
}
