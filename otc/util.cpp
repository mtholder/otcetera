// UTF-8 parsing from http://stackoverflow.com/questions/4775437/read-unicode-utf-8-file-into-wstring
// http://stackoverflow.com/questions/4804298/how-to-convert-wstring-into-string
#include <clocale>
#include <cstdlib>
#include <fstream>
#include <locale>
#include <iostream>
#include <string>
#include <vector>

#include "otc/util.h"

namespace otc {

const std::string readStrContentOfUTF8File(const std::string &filepath) {
	const std::wstring utf8content = readWStrContentOfUTF8File(filepath);
	const std::locale empty_locale("");
	typedef std::codecvt<wchar_t, char, std::mbstate_t> converter_type;
	const converter_type & converter = std::use_facet<converter_type>(empty_locale);
	std::vector<char> to((unsigned long)utf8content.length() * (unsigned long)converter.max_length());
	std::mbstate_t state;
	const wchar_t* from_next;
	char* to_next;
	const converter_type::result result = converter.out(state,
														utf8content.data(),
														utf8content.data() + utf8content.length(),
														from_next,
														&to[0],
														&to[0] + to.size(),
														to_next);
	if (result == converter_type::ok or result == converter_type::noconv) {
		const std::string ccontent(&to[0], to_next);
		return ccontent;
	}
	throw OTCError("Error reading the contents of filepath as UTF-8");
}

bool openUTF8File(const std::string &filepath, std::ifstream & inp) {
	inp.open(filepath);
	return inp.good();
}
bool openUTF8WideFile(const std::string &filepath, std::wifstream & inp) {
	std::setlocale(LC_ALL, "");
	const std::locale empty_locale("");
	typedef std::codecvt<wchar_t, char, std::mbstate_t> converter_type;
	const converter_type & converter = std::use_facet<converter_type>(empty_locale);
	const std::locale utf8_locale = std::locale(empty_locale, &converter);
	inp.open(filepath);
	inp.imbue(utf8_locale);
	return inp.good();
}


}//namespace otc