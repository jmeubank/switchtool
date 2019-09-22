#ifndef SNMP_HPP_INC
#define SNMP_HPP_INC


#include <string>


struct SNMPCallback {
	virtual ~SNMPCallback() {}
	virtual void OnData(const std::string& num, const std::string& val) = 0;
};

void SNMPWalk(int version, const std::string& community, const std::string& ip,
const std::string& oid, SNMPCallback* scb = 0);
std::string SNMPUnSTRING(const std::string& value);


#endif
