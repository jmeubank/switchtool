#ifndef HOST_HPP_INC
#define HOST_HPP_INC


#include "common.hpp"


struct HostFactory;


class Host {
public:
	static Host* Construct(const Boss& boss, const PropTree& phost);

	static std::map< std::string, HostFactory* >* GetFactories();

	Host(const Boss& boss, const PropTree& phost) :
	m_boss(boss),
	m_phost(phost)
	{}
	virtual ~Host() {}

	virtual void Execute(const std::string& cmd, const std::string& args) = 0;

protected:
	const Boss& m_boss;
	PropTree m_phost;
};


struct HostFactory {
	virtual ~HostFactory() {}
	virtual Host* Construct(const Boss& boss, const PropTree& phost) = 0;
};

template< class T >
struct HostFactoryRegistrant : public HostFactory {
	HostFactoryRegistrant(const std::string& switchtype) {
		Host::GetFactories()->insert(std::pair< std::string, HostFactory* >(
			switchtype,
			this
		));
	}
	virtual Host* Construct(const Boss& boss, const PropTree& phost) {
		return new T(boss, phost);
	}
};


#endif
