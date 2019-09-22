/* File: proptree.hpp
 *
 * Created: JohnE, 2013-02-24
 * 
 * Representing a fantastic case of me not finding quite what I needed in
 * another library at the time I wrote it, PropTree is a simple JSON-like
 * data structure for C++, or "property tree": a key-value hierarchy
 * where the keys are strings and the values are either strings or child
 * PropTree objects.
 * 
 */
#ifndef SWITCHTOOL_PROPTREE_HPP_INC
#define SWITCHTOOL_PROPTREE_HPP_INC

#include <cstdio>
#include <vector>
#include <map>
#include <string>

class PropTree;

/* This fun little "choose" idiom implements true/false as a template parameter in order
 * to easily define both const and non-const iterators for the PropTree in a single
 * template declaration.
 */
// First, the non-instantiated template version of "choose"
template< bool flag, class IsTrue, class IsFalse >
struct choose;
// Then, instantiate it for the case of "true"
template< class IsTrue, class IsFalse >
struct choose< true, IsTrue, IsFalse > {
	typedef IsTrue type;
};
// Finally, instantiate it for the case of "false"
template< class IsTrue, class IsFalse >
struct choose< false, IsTrue, IsFalse > {
	typedef IsFalse type;
};

/* For readability, typedef a vector that will hold all children of a PropTree
 * as key/value pairs.
 */
typedef std::vector<
	std::pair< std::string, PropTree >
> PropTreeChildrenArray;
/* For readability, typedef a map that will remember which offset in a vector
 * holds a given PropTree child, for a given key. See below; this is a O(n)
 * space tradeoff to gain O(log(n)) search time.
 */
typedef std::map< std::string, size_t > PropTreeChildrenMap;

/* This class allows iteration through the children of a PropTree object. */
template< bool isconst = false >
class PropTreeIterator {
public:
	/* For readability, create PropTreeRef, PropTreePtr and
	 * ChildrenArrayIterType typedefs that are const or not depending on
	 * template.
	 */
	typedef typename choose<
		isconst,
		const PropTree&,
		PropTree&
	>::type PropTreeRef;
	typedef typename choose<
		isconst,
		const PropTree*,
		PropTree*
	>::type PropTreePtr;
	typedef typename choose<
		isconst,
		PropTreeChildrenArray::const_iterator,
		PropTreeChildrenArray::iterator
	>::type ChildrenArrayIterType;

	/* The default constructor does nothing. */
	PropTreeIterator() {}

	/* The copy constructor may only clone from a non-const iterator. */
	PropTreeIterator(const PropTreeIterator< false >& i)
	 : m_array_iter(i.m_array_iter) {
	}

	/* To meet the requirements of the forward iterator interface,
	 * define dereference and increment operations.
	 */
	PropTreeRef operator * () const {
		return m_array_iter->second;
	}
	PropTreePtr operator -> () const {
		return &(m_array_iter->second);
	}
	PropTreeIterator& operator ++ () {
		++m_array_iter;
		return *this;
	}
	PropTreeIterator operator ++ (int) {
		PropTreeIterator tmp(*this);
		++m_array_iter;
		return tmp;
	}

	/* This function returns the corresponding key of the child
	 * currently pointed to by the iterator.
	 */
	std::string const& GetKey() const {
		return m_array_iter->first;
	}

	/* Equality/inequality operators */
	friend bool operator == (const PropTreeIterator& x,
	const PropTreeIterator& y) {
		return (x.m_array_iter == y.m_array_iter);
	}
	friend bool operator != (const PropTreeIterator& x,
	const PropTreeIterator& y) {
		return (x.m_array_iter != y.m_array_iter);
	}

private:
	friend class PropTree;

	/* This private constructor is called by the PropTree class to create
	 * an iterator pointed at its first child.
	 */
	PropTreeIterator(PropTreePtr referenced, ChildrenArrayIterType array_iter)
	 : m_array_iter(array_iter) {
	}

	/* This is a vector iterator with O(k) time and O(k) space performance
	 * per increment operation. */
	ChildrenArrayIterType m_array_iter;
};

class PropTree
{
public:
	// Types
	typedef PropTreeIterator< false > iterator;
	typedef PropTreeIterator< true > const_iterator;

	// Static functions
	static PropTree FromJson(std::string const& json_string);

	// Constructors
	PropTree() {}

	PropTree(std::string const& data)
	 : m_data(data)
	{}

	PropTree(PropTree const& c)
	 : m_data(c.m_data),
	 m_children_array(c.m_children_array),
	 m_children_map(c.m_children_map)
	{}

	// Symbol operators
	PropTree& operator = (PropTree const& c)
	{
		this->m_data = c.m_data;
		this->m_children_array = c.m_children_array;
		this->m_children_map = c.m_children_map;
		return *this;
	}
	PropTree& operator = (std::string const& data) {
		this->m_data = data;
		this->m_children_array.clear();
		this->m_children_map.clear();
		return *this;
	}

	template< class T >
	PropTree& operator [] (T const& key) {
		return this->at(key);
	}
	template< class T >
	PropTree const& operator [] (T const& key) const {
		return this->at(key);
	}

	// Cast operators
	operator std::string () const {
		return this->m_data;
	}

	// Comparison operators
	friend bool operator == (const PropTree& x, const std::string& y) {
		return (x.m_data == y);
	}
	friend bool operator != (const PropTree& x, const std::string& y) {
		return (x.m_data != y);
	}

	// Property functions
	bool HasChildren() const {
		return !(this->m_children_array.empty());
	}

	bool IsArray() const {
		return this->m_children_map.empty();
	}

	bool ChildExists(std::string const& child) const {
		return (this->m_children_map.find(child) != this->m_children_map.end());
	}

	// Accessors
	PropTree const& at(size_t idx) const {
		return this->m_children_array[idx].second;
	}
	PropTree& at(size_t idx) {
		return this->m_children_array[idx].second;
	}
	PropTree const& at(std::string const& key) const {
		PropTreeChildrenMap::const_iterator fd = this->m_children_map.find(key);
		if (fd == this->m_children_map.end()) {
			static PropTree empty;
			return empty;
		} else
			return this->m_children_array[fd->second].second;
	}
	PropTree& at(std::string const& key) {
		PropTreeChildrenMap::iterator fd = this->m_children_map.find(key);
		if (fd == this->m_children_map.end()) {
			this->m_children_map.insert(
				std::make_pair(key, this->m_children_array.size())
			);
			this->m_children_array.push_back(std::make_pair(key, PropTree()));
			return this->m_children_array.back().second;
		} else
			return this->m_children_array[fd->second].second;
	}

	PropTree& ArrayPushBack(PropTree const& proptree) {
		this->m_children_array.push_back(
			std::make_pair(std::string(), proptree)
		);
		return this->m_children_array.back().second;
	}

	std::string GetData() const {
		return this->m_data;
	}
	void SetData(std::string const& data) {
		this->m_data = data;
	}

	// Iterator functions
	iterator Begin() {
		return iterator(this, m_children_array.begin());
	}
	const_iterator Begin() const {
		return const_iterator(this, m_children_array.begin());
	}
	iterator End() {
		return iterator(this, m_children_array.end());
	}
	const_iterator End() const {
		return const_iterator(this, m_children_array.end());
	}

private:
	friend class PropTreeIterator< false >;
	friend class PropTreeIterator< true >;

	std::string m_data;
	PropTreeChildrenArray m_children_array;
	PropTreeChildrenMap m_children_map;
};

#endif // SWITCHTOOL_PROPTREE_HPP_INC
