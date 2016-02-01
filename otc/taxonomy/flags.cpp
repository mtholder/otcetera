#include "otc/taxonomy/flags.h"

// TODO: write out a reduced taxonomy

#include <iostream>
#include <exception>
#include <vector>
#include <cstdlib>
#include <unordered_map>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/spirit/include/qi_symbols.hpp>
#include <boost/utility/string_ref.hpp>
#include <bitset>

#include "otc/error.h"
#include "otc/tree.h"
#include "otc/tree_operations.h"
#include "otc/taxonomy/flags.h"

using namespace otc;

using std::string;
using std::vector;
using std::cout;
using std::cerr;
using std::endl;
using std::bitset;

using boost::spirit::qi::symbols;
using boost::string_ref;
using namespace boost::spirit;

// https://github.com/OpenTreeOfLife/taxomachine/blob/master/src/main/java/org/opentree/taxonomy/OTTFlag.java

// http://www.boost.org/doc/libs/1_50_0/libs/spirit/doc/html/spirit/qi/reference/string/symbols.html

auto get_symbols()
{
    symbols<char, int> sym;

    sym.add
        ("not_otu", 0)
        ("environmental", 1)
        ("environmental_inherited", 2)
        ("viral", 3)
        ("hidden", 4)
        ("hidden_inherited", 5)
//        unclassified_direct
        ("was_container", 6)
        ("barren", 8)
        ("extinct", 9)
//        extinct_direct)
        ("extinct_inherited", 11)
        ("major_rank_conflict", 12)
//        major_rank_conflict_direct
        ("major_rank_conflict_inherited", 14)
        ("unclassified", 15)
        ("unclassified_inherited", 16)
        ("edited", 17)
        ("hybrid", 18)
        ("incertae_sedis", 19)
        ("incertae_sedis_inherited", 20)
//	      incertae_sedis_direct
        ("infraspecific", 22)
        ("sibling_lower", 23)
        ("sibling_higher", 24)
        ("tattered", 25)
        ("tattered_inherited", 26)
        ("forced_visible", 27)
        ("unplaced", 28)
        ("unplaced_inherited", 29)
        ("inconsistent", 30)
        ("merged", 31)
        ;
    return sym;
}

auto flag_symbols = get_symbols();

namespace otc
{
    std::bitset<32> flag_from_string(const char* start, const char* end)
    {
        int n = end - start;
        assert(n >= 0);
        bitset<32> flags;
        if (n > 0)
        {
            int flag = 0;
            boost::spirit::qi::parse(start, end, flag_symbols, flag);
            if (start != end)
            {
                std::cout<<"fail!";
                std::abort();
            }
            flags.set(flag);
        }
        return flags;
    }

    std::bitset<32> flags_from_string(const char* start, const char* end)
    {
        assert(start <= end);

        bitset<32> flags;
        while (start < end)
        {
            assert(start <= end);
            const char* sep = std::strchr(start, ',');
            if (not sep) sep = end;
            flags |= flag_from_string(start, sep);
            start = sep + 1;
        }
        return flags;
    }

    std::bitset<32> flags_from_string(const string& s)
    {
        const char* start = s.c_str();
        const char* end = start + s.length();
        return flags_from_string(start, end);
    }

    std::bitset<32> cleaning_flags_from_config_file(const string& filename)
    {
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(filename, pt);
        string cleaning_flags_string = pt.get<std::string>("taxonomy.cleaning_flags");
        return flags_from_string(cleaning_flags_string);
    }

}
