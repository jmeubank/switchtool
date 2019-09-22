/* File: proptree.cpp
 *
 * Created: JohnE, 2013-02-24
 */


#include "proptree.hpp"

#include <stack>
#include <sstream>
#include "common.hpp"
#include "yajl/yajl_parse.h"


struct JsonPropTreeParser {
	std::stack< std::pair< PropTree*, size_t > > where;
	PropTree* editing;

	JsonPropTreeParser(PropTree& populate)
	 : editing(&populate) {
	}

	static int OnNull(void* vme) {
		JsonPropTreeParser* me = static_cast< JsonPropTreeParser* >(vme);
		me->editing->SetData("");
		me->MaybeIncrementArrayKey();
		return 1;
	}
	static int OnBool(void* vme, int val) {
		JsonPropTreeParser* me = static_cast< JsonPropTreeParser* >(vme);
		me->editing->SetData(val ? "1" : "0");
		me->MaybeIncrementArrayKey();
		return 1;
	}
	static int OnNumber(void* vme, const char* val_start, size_t val_len) {
		return JsonPropTreeParser::OnString(vme,
		reinterpret_cast< const unsigned char* >(val_start), val_len);
	}
	static int OnString(void* vme, const unsigned char* val_start,
	size_t val_len) {
		JsonPropTreeParser* me = static_cast< JsonPropTreeParser* >(vme);
		me->editing->SetData(
			std::string(reinterpret_cast< const char* >(val_start), val_len)
		);
		me->MaybeIncrementArrayKey();
		return 1;
	}
	static int OnMapStart(void* vme) {
		JsonPropTreeParser* me = static_cast< JsonPropTreeParser* >(vme);
		me->where.push(std::make_pair(me->editing, 0UL));
		return 1;
	}
	static int OnMapKey(void* vme, const unsigned char* key_start,
	size_t key_len) {
		JsonPropTreeParser* me = static_cast< JsonPropTreeParser* >(vme);
		me->editing = &((*(me->where.top().first))[
			std::string(reinterpret_cast< const char* >(key_start), key_len)
		]);
		return 1;
	}
	static int OnMapEnd(void* vme) {
		JsonPropTreeParser* me = static_cast< JsonPropTreeParser* >(vme);
		me->editing = me->where.top().first;
		me->where.pop();
		me->MaybeIncrementArrayKey();
		return 1;
	}
	static int OnArrayStart(void* vme) {
		JsonPropTreeParser* me = static_cast< JsonPropTreeParser* >(vme);
		me->where.push(std::make_pair(me->editing, 1UL));
		me->editing = &((*(me->editing))["0"]);
		return 1;
	}
	static int OnArrayEnd(void* vme) {
		JsonPropTreeParser* me = static_cast< JsonPropTreeParser* >(vme);
		me->editing = me->where.top().first;
		me->where.pop();
		me->MaybeIncrementArrayKey();
		return 1;
	}

	void MaybeIncrementArrayKey() {
		if (this->where.size() > 0 && this->where.top().second > 0) {
			std::ostringstream oss;
			oss << this->where.top().second;
			this->editing = &((*(this->where.top().first))[oss.str()]);
			++(this->where.top().second);
		}
	}
};

PropTree PropTree::FromJson(std::string const& json_string) {
	yajl_callbacks ycb = {
		JsonPropTreeParser::OnNull,
		JsonPropTreeParser::OnBool,
		0,
		0,
		JsonPropTreeParser::OnNumber,
		JsonPropTreeParser::OnString,
		JsonPropTreeParser::OnMapStart,
		JsonPropTreeParser::OnMapKey,
		JsonPropTreeParser::OnMapEnd,
		JsonPropTreeParser::OnArrayStart,
		JsonPropTreeParser::OnArrayEnd
	};
	PropTree ret;
	JsonPropTreeParser jp(ret);
	yajl_handle yh = yajl_alloc(&ycb, 0, &jp);
	if (yajl_parse(
		yh,
		reinterpret_cast< const unsigned char* >(json_string.c_str()),
		json_string.length()
	) != yajl_status_ok
	|| yajl_complete_parse(yh) != yajl_status_ok) {
		unsigned char* err = yajl_get_error(
			yh,
			1,
			reinterpret_cast< const unsigned char* >(json_string.c_str()),
			json_string.length()
		);
		std::string to_throw = fmt("Unable to parse command input as JSON:\n%s",
		reinterpret_cast< char* >(err));
		yajl_free_error(yh, err);
		yajl_free(yh);
		throw to_throw;
	}
	yajl_free(yh);
	return ret;
}
