#ifndef OTCETERA_UTIL_H
#define OTCETERA_UTIL_H

#include <iostream>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <iterator>
#include <list>
#include "otc/otc_base_includes.h"
#include "otc/error.h"

namespace otc {

const std::string readStrContentOfUTF8File(const std::string &filepath);
const std::wstring readWStrContentOfUTF8File(const std::string &filepath);
bool openUTF8File(const std::string &filepath, std::ifstream & inp);
bool openUTF8WideFile(const std::string &filepath, std::wifstream & inp);


template<typename T>
bool isProperSubset(const T & small, const T & big);
template<typename T>
std::set<T> set_difference_as_set(const std::set<T> & small, const std::set<T> & big);
template<typename T, typename U>
bool contains(const T & container, const U & key);
template<typename T, typename U>
std::set<T> keys(const std::map<T, U> & container);
void writeOttSet(std::ostream & out, const char *indent, const std::set<long> &fir, const char * sep);
void writeOttSetDiff(std::ostream & out, const char *indent, const std::set<long> &fir, const char *firN, const std::set<long> & sec, const char *secN);

inline const std::wstring readWStrContentOfUTF8File(const std::string &filepath) {
	std::wifstream inp;
	if (!openUTF8WideFile(filepath, inp)) {
		throw OTCError("Could not open \"" + filepath + "\"");
	}
	const std::wstring utf8content((std::istreambuf_iterator<wchar_t>(inp) ), (std::istreambuf_iterator<wchar_t>()));
	return utf8content;
}

template<typename T, typename U>
bool contains(const T & container, const U & key) {
	return container.find(key) != container.end();
}

template<typename T, typename U>
std::set<T> keys(const std::map<T, U> & container) {
	std::set<T> k;
	for (auto x : container) {
		k.insert(x.first);
	}
	return k;
}


inline void writeOttSet(std::ostream & out,
						const char *indent,
						const std::set<long> &fir,
						const char * sep) {
	for (auto rIt = fir.begin(); rIt != fir.end(); ++rIt) {
		if (rIt != fir.begin()) {
			out << sep;
		}
		out << indent << "ott" << *rIt;
	}
}

inline void writeOttSetDiff(std::ostream & out,
							const char *indent,
							const std::set<long> &fir,
							const char *firN,
							const std::set<long> & sec,
							const char *secN) {
	for (auto rIt : fir) {
		if (sec.find(rIt) == sec.end()) {
			out << indent << "ott" << rIt << " is in " << firN << " but not " << secN << "\n";
		}
	}
	for (auto rIt : sec) {
		if (fir.find(rIt) == fir.end()) {
			out << indent << "ott" << rIt << " is in " << secN << " but not " << firN << "\n";
		}
	}
}

template<typename T>
inline bool isProperSubset(const T & small, const T & big) {
	if (big.size() <= small.size()) {
		return false;
	}
	for (auto rIt : small) {
		if (big.find(rIt) == big.end()) {
			return false;
		}
	}
	return true;
}

template<typename T>
std::set<T> set_difference_as_set(const std::set<T> & fir, const std::set<T> & sec) {
	std::set<T> d;
	set_difference(begin(fir), end(fir), begin(sec), end(sec), std::inserter(d, d.end()));
	return d;
}

bool char_ptr_to_long(const char *c, long *n);
std::size_t find_first_graph_index(const std::string & s);
std::size_t find_last_graph_index(const std::string & s);
std::string strip_leading_whitespace(const std::string & s);
std::string strip_trailing_whitespace(const std::string & s);
std::string strip_surrounding_whitespace(const std::string &n);
std::list<std::string> split_string(const std::string &s);
std::list<std::set<long> > parseDesignatorsFile(const std::string &fp);


inline std::size_t find_first_graph_index(const std::string & s) {
	std::size_t pos = 0U;
	for (auto c : s) {
		if (isgraph(c)) {
			return pos;
		}
		++pos;
	}
	return std::string::npos;
}
inline std::size_t find_last_graph_index(const std::string & s) {
	auto pos = s.length();
	while (pos > 0) {
		--pos;
		if (isgraph(s[pos])) {
			return pos;
		}
	}
	return std::string::npos;
}


inline std::string strip_leading_whitespace(const std::string & n) {
	auto x = find_first_graph_index(n);
	if (x == std::string::npos) {
		return std::string();
	}
	return n.substr(x);
}

inline std::string strip_trailing_whitespace(const std::string & n) {
	auto x = find_last_graph_index(n);
	if (x == std::string::npos) {
		return std::string();
	}
	return n.substr(0, 1 + x);
}

inline std::string strip_surrounding_whitespace(const std::string &n) {
	auto s = find_first_graph_index(n);
	if (s == std::string::npos) {
		return std::string();
	}
	auto e = find_last_graph_index(n);
	assert(e != std::string::npos);
	return n.substr(s, 1 + e - s);
}

} //namespace otc
#endif
