// TODO: https://techoverflow.net/blog/2013/03/31/mmap-with-boost-iostreams-a-minimalist-example/

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

using Tree_t = RootedTree<RTNodeNoData, RTreeNoData>;

// 1. Write a parser to read the lines faster
// 2. Avoid memory allocation -- by mmapping the taxonomy file?
// 3. Convert the flags into a bitmask
// 4. Should the Rank be a converted to an integer?
// 5. Can we assign OTT IDs to internal nodes of a tree while accounting for Incertae Sedis taxa?
// * What are the triplet-inference rules for the Incertae Sedis problem?

namespace po = boost::program_options;
using po::variables_map;

variables_map parse_cmd_line(int argc,char* argv[]) 
{ 
  using namespace po;

  // named options
  options_description invisible("Invisible options");
  invisible.add_options()
    ("taxonomy", value<string>(),"Filename for the taxonomy")
    ;

  options_description visible("All options");
  visible.add_options()
      ("help,h", "Produce help message")
      ("config,c",value<string>(),"Config file containing flags to filter")
      ("clean",value<string>(),"Comma-separated string of flags to filter")
      ("write-tree,T","Write out the result as a tree")
      ("root,r", value<int>(), "OTT id of root node of subtree to keep")
//    ("quiet,q","QUIET mode (all logging disabled)")
//    ("trace,t","TRACE level debugging (very noisy)")
//    ("verbose,v","verbose")
    ;

  options_description all("All options");
  all.add(invisible).add(visible);

  // positional options
  positional_options_description p;
  p.add("taxonomy", -1);

  variables_map args;     
  store(command_line_parser(argc, argv).options(all).positional(p).run(), args);
  notify(args);    

  if (args.count("help")) {
    cout<<"Usage: taxonomy-parser <taxonomy-dir> [OPTIONS]\n";
    cout<<"Select columns from a Tracer-format data file.\n\n";
    cout<<visible<<"\n";
    exit(0);
  }

  return args;
}

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

auto flag_from_string(const char* start, const char* end)
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

long n_nodes(const Tree_t& T) {
#pragma clang diagnostic ignored  "-Wunused-variable"
#pragma GCC diagnostic ignored  "-Wunused-variable"
    long count = 0;
    for(auto nd: iter_post_const(T)){
        count++;
    }
    return count;
}

auto flags_from_string(const char* start, const char* end)
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

auto flags_from_string(const string& s)
{
    const char* start = s.c_str();
    const char* end = start + s.length();
    return flags_from_string(start, end);
}

struct taxonomy_record
{
    string line;
    long id = 0;
    long parent_id = 0;
    int parent_index = 0;
    string_ref name;
    string_ref rank;
    string_ref sourceinfo;
    string_ref uniqname;
    bitset<32> flags;
    bitset<32> marks;
    Tree_t::node_type* node_ptr = nullptr;
    taxonomy_record(taxonomy_record&& tr) = default;
    explicit taxonomy_record(const string& line);
};

taxonomy_record::taxonomy_record(const string& line_)
    :line(line_)
{
    // parse the line
    // also see boost::make_split_iterator
    const char* start[8];
    const char* end[8];

    start[0] = line.c_str();
    for(int i=0;i<7;i++)
    {
        end[i] = std::strstr(start[i],"\t|\t");
        start[i+1] = end[i] + 3;
    }

//    boost::container::small_vector<string_ref,10> words;
//    for (string_ref&& r : iter_split(v, line, token_finder(is_any_of(","))) |
//             transformed([](R const& r){return boost::string_ref(&*r.begin(), r.size());})
//        )
//        words.push_back(r);

    char *temp;
    id = std::strtoul(start[0], &temp, 10);
    parent_id = std::strtoul(start[1], &temp, 10);
    name = string_ref(start[2],end[2]-start[2]);
    rank = string_ref(start[3],end[3]-start[3]);
    sourceinfo = string_ref(start[4],end[4]-start[4]);
    uniqname = string_ref(start[5],end[5]-start[5]);
    flags = flags_from_string(start[6],end[6]);
}

void propagate_marks_to_descendants(vector<taxonomy_record>& taxonomy)
{
    for(auto& record: taxonomy)
        if (record.parent_id)
            record.marks |= taxonomy[record.parent_index].marks;
}

void mark_taxonomy_with_cleaning_flags(vector<taxonomy_record>& taxonomy, bitset<32> cleaning_flags, int bit)
{
    int matched = 0;
    for(auto& record: taxonomy)
        if ((record.flags & cleaning_flags).any())
        {
            matched++;
            record.marks.set(bit);
        }
    std::cerr<<"#lines directly matching cleaning flags = "<<matched<<std::endl;
}

std::unique_ptr<Tree_t> tree_from_taxonomy(vector<taxonomy_record>& taxonomy)
{
    std::unique_ptr<Tree_t> tree(new Tree_t);
    for(int i=0;i<taxonomy.size();i++)
    {
        const auto& line = taxonomy[i];

        if (line.marks.test(1)) continue;
        if (not line.marks.test(2)) continue;

        // Make the tree
        Tree_t::node_type* nd = nullptr;
        if (not line.parent_id)
            nd = tree->createRoot();
        else if (not taxonomy[line.parent_index].marks.test(2))
            nd = tree->createRoot();
        else
        {
            auto parent_nd = taxonomy[line.parent_index].node_ptr;
            nd = tree->createChild(parent_nd);
        }
        nd->setOttId(line.id);
        nd->setName(string(line.name));
        taxonomy[i].node_ptr = nd;
    }
    cerr<<"#tree nodes = "<<n_nodes(*tree)<<std::endl;
    return tree;
}

auto cleaning_flags_from_config_file(const string& filename)
{
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(filename, pt);
    string cleaning_flags_string = pt.get<std::string>("taxonomy.cleaning_flags");
    return flags_from_string(cleaning_flags_string);
}

struct Taxonomy: public vector<taxonomy_record>
{
    int root = -1;
    std::unordered_map<long,int> index;
    string path;
    Taxonomy(const string& dir);
};

Taxonomy::Taxonomy(const string& dir)
    :path(dir)
{
    string filename = dir + "/taxonomy.tsv";

    std::ifstream taxonomy_stream(filename);
    if (not taxonomy_stream)
        throw OTCError()<<"Could not open file '"<<filename<<"'.";

    string line;
    int count = 0;
    std::getline(taxonomy_stream,line);
    if (line != "uid\t|\tparent_uid\t|\tname\t|\trank\t|\tsourceinfo\t|\tuniqname\t|\tflags\t|\t")
        throw OTCError()<<"First line of file '"<<filename<<"' is not a taxonomy header.";

    while(std::getline(taxonomy_stream,line))
    {
        // Add line to vector
        int my_index = size();
        emplace_back(line);
        auto& record = back();
        index[record.id] = my_index;
        if (record.parent_id)
            record.parent_index = index.at(record.parent_id);
        else
            root = my_index;
        count++;
    }
    cerr<<"#lines = "<<count<<std::endl;
}

int main(int argc, char* argv[])
{
    std::ios::sync_with_stdio(false);

    try
    {
        variables_map args = parse_cmd_line(argc,argv);

        unsigned keep_root = -1;
        if (args.count("root"))
            keep_root = args["root"].as<int>();
        
        bitset<32> cleaning_flags = 0;
        if (args.count("config"))
            cleaning_flags |= cleaning_flags_from_config_file(args["config"].as<string>());
        if (args.count("clean"))
            cleaning_flags |= flags_from_string(args["clean"].as<string>());

        string taxonomy_dir = args["taxonomy"].as<string>();

        Taxonomy taxonomy(taxonomy_dir);

        // Mark records with bit #1 if they match the cleaning flags
        mark_taxonomy_with_cleaning_flags(taxonomy, cleaning_flags, 1);

        // Mark the root
        if (keep_root == -1)
            taxonomy[taxonomy.root].marks.set(2);
        else if (not taxonomy.index.count(keep_root))
            throw OTCError()<<"Can't find root id '"<<keep_root<<"'";
        else
            taxonomy[taxonomy.index.at(keep_root)].marks.set(2);

        // Propagate marks
        propagate_marks_to_descendants(taxonomy);

        if (args.count("write-tree"))
            writeTreeAsNewick(cout, *tree_from_taxonomy(taxonomy));
        std::cout<<std::endl;
    }
    catch (std::exception& e)
    {
        cerr<<"otc-taxonomy-parser: Error! "<<e.what()<<std::endl;
        exit(1);
    }
}
