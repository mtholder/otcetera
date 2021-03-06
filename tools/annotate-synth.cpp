// See https://github.com/OpenTreeOfLife/opentree/wiki/Open-Tree-of-Life-APIs-v3#synthetic-tree
#include <iostream>
#include <tuple>
#include <sstream>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include "json.hpp"
#include "otc/conflict.h"
#include "otc/otcli.h"
#include "otc/supertree_util.h"
#include "otc/tree_operations.h"

using namespace otc;
using json = nlohmann::json;
using std::vector;
template <typename T, typename U>
using Map = std::unordered_multimap<T,U>;
template <typename T, typename U>
using map = std::unordered_map<T,U>;
template <typename T>
using set = std::unordered_set<T>;
using std::string;
//using std::map;
using std::pair;
using std::tuple;
using std::unique_ptr;
using std::string;

namespace po = boost::program_options;
using po::variables_map;

// TODO: Could we exemplify tips here, if we had access to the taxonomy?
variables_map parse_cmd_line(int argc,char* argv[])  {
    using namespace po;
    // named options
    options_description invisible("Invisible options");
    invisible.add_options()
        ("synth", value<string>(),"Filename for the synthesis tree")
        ("input", value<vector<string>>()->composing(),"Filename for input trees")
        ;

    options_description reporting("Reporting options");
    reporting.add_options()
        ("ignore-monotypic,I",value<string>(),"Ignore monotypic nodes in summary")
        ;
    options_description visible;
    visible.add(otc::standard_options());
    // positional options
    positional_options_description p;
    p.add("synth", 1);
    p.add("input", -1);
    variables_map vm = otc::parse_cmd_line_standard(argc, argv,
                                                    "Usage: otc-annotate-synth <synth-tree> <input tree1> <input tree2> ... [OPTIONS]\n"
                                                    "Annotate the synthesis tree with support & conflict information from the input trees.",
                                                    visible, invisible, p);
    return vm;
}

namespace std
{
template<typename T, typename U>
struct hash<pair<T,U>> {
    std::size_t operator()(const std::pair<T,U>& p) const noexcept {return std::hash<T>()(p.first) * std::hash<U>()(p.second);}
};

}

using Tree_t = ConflictTree;
using node_t = Tree_t::node_type;

void compute_summary_leaves(Tree_t& tree, const map<OttId,Tree_t::node_type*>& summaryOttIdToNode);
string get_source_node_name_if_available(const Tree_t::node_type* node);

// uses the OTT Ids in `tree` to fill in the `summary_node` field of each leaf
void compute_summary_leaves(Tree_t& tree, const map<OttId,Tree_t::node_type*>& summaryOttIdToNode) {
    for(auto leaf: iter_leaf(tree)) {
        summary_node(leaf) = summaryOttIdToNode.at(leaf->get_ott_id());
    }
}


string get_source_node_name_if_available(const Tree_t::node_type* node) {
    string name = node->get_name();
    auto source = get_source_node_name(name);
    return (source ? *source : name);
}

json get_support_blob_as_array(const Map<string,string>& M) {
    json support_blob = json::object();
    auto m_it = M.begin();
    for (;  m_it != M.end();  ) {
        string source_id = (*m_it).first;
        json nodes_array = json::array();
        // Iterate over all map elements with key == source_id
        auto keyRange = M.equal_range(source_id);
        for (auto s_it = keyRange.first;  s_it != keyRange.second;  ++s_it) {
            nodes_array.push_back((*s_it).second);
        }
        // This wastes time, but makes the output order predictable
        std::sort(nodes_array.begin(), nodes_array.end());
        support_blob[source_id] = nodes_array;
        m_it = keyRange.second;
    }
    return support_blob;
}

json get_support_blob_as_single_element(const Map<string,string>& M) {
    json support_blob = json::object();
    auto m_it = M.begin();
    for (;  m_it != M.end();  ) {
        string source_id = (*m_it).first;
        json element;
        // Iterate over all map elements with key == source_id
        auto keyRange = M.equal_range(source_id);
        int count = 0;
        for (auto s_it = keyRange.first;  s_it != keyRange.second;  ++s_it) {
            element = (*s_it).second;
            ++count;
        }
        assert(count == 1);
        support_blob[source_id] = element;
        m_it = keyRange.second;
    }
    return support_blob;
}

void set_support_blob_as_array(json& j, const map<string,Map<string,string>>& m, const string& field, const string& name) {
    json support_blob;
    auto it = m.find(name);
    if (it != m.end()) {
        support_blob = get_support_blob_as_array(it->second);
    }
    if (not support_blob.empty()) {
        j[field] = support_blob;
    }
}

void set_support_blob_as_single_element(json& j, const map<string,Map<string,string>>& m, const string& field, const string& name) {
    json support_blob;
    auto it = m.find(name);
    if (it != m.end()) {
        support_blob = get_support_blob_as_single_element(it->second);
    }
    if (not support_blob.empty()) {
        j[field] = support_blob;
    }
}

void add_element(map<string, Map<string, string>>& m, map<string, set<pair<string,string>>>& s,
                 const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const string& source) {
    string synth = synth_node->get_name();
    string node = get_source_node_name_if_available(input_node);
    pair<string,string> x{source, node};
    if (not s[synth].count(x)) {
        s[synth].insert(x);
        m[synth].insert(x);
    }
}

map<string,string> suppress_and_record_monotypic(Tree_t& tree) {
    map<string,string> to_child;
    std::vector<Tree_t::node_type*> remove;
    for (auto nd:iter_post(tree)) {
        if (nd->is_outdegree_one_node()) {
            remove.push_back(nd);
        }
    }
    for (auto nd: remove) {
        if (nd->get_name().size()) {
            auto child = nd->get_first_child();
            assert(not child->is_outdegree_one_node());
            assert(child->get_name().size());
            assert(to_child.count(nd->get_name()) == 0);
            to_child[nd->get_name()] = child->get_name();
        }
        del_monotypic_node(nd,tree);
    }
    return to_child;
}

map<string, Map<string,string>> supported_by;
map<string, Map<string,string>> partial_path_of;
map<string, Map<string,string>> conflicts_with;
map<string, Map<string,string>> resolves;
map<string, Map<string,string>> resolved_by;
map<string, Map<string,string>> terminal;
map<string, set<pair<string, string>>> supported_by_set;
map<string, set<pair<string, string>>> partial_path_of_set;
map<string, set<pair<string, string>>> conflicts_with_set;
map<string, set<pair<string, string>>> resolves_set;
map<string, set<pair<string, string>>> resolved_by_set;
map<string, set<pair<string, string>>> terminal_set;
int numErrors = 0;
bool headerEmitted = false;

void set_terminal(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const string& source) {
    add_element(terminal, terminal_set, synth_node, input_node, source);
}

void set_supported_by(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const string& source) {
    add_element(supported_by, supported_by_set, synth_node, input_node, source);
}

void set_partial_path_of(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const string& source) {
    add_element(partial_path_of, partial_path_of_set, synth_node, input_node, source);
}

void set_conflicts_with(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const string& source) {
    add_element(conflicts_with, conflicts_with_set, synth_node, input_node, source);
}

void set_resolved_by(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const string& source) {
    add_element(resolved_by, resolved_by_set, synth_node, input_node, source);
}

void set_resolves(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const string& source) {
    add_element(resolves, resolves_set, synth_node, input_node, source);
}

json gen_json(const Tree_t& summaryTree, const map<string,string>& monotypic_nodes, bool ignore_monotypic) {
    json document;
    document["num_tips"] = count_leaves(summaryTree);
    document["root_ott_id"] = summaryTree.get_root()->get_ott_id();    
    json nodes;
    for(auto nd: iter_post_const(summaryTree)) {
        json node;
        string name = nd->get_name();
        set_support_blob_as_single_element(node, terminal, "terminal", name);
        set_support_blob_as_single_element(node, supported_by, "supported_by", name);
        set_support_blob_as_single_element(node, partial_path_of, "partial_path_of", name);
        set_support_blob_as_array(node, conflicts_with, "conflicts_with", name);
        set_support_blob_as_single_element(node, resolves, "resolves", name);
        set_support_blob_as_array(node, resolved_by, "resolved_by", name);
        if (not node.empty()) {
            nodes[name] = node;
        }
    }
    // Copy support information to monotypic nodes from their first non-monotypic descendant
    if (not ignore_monotypic) {
        for(const auto& m: monotypic_nodes) {
            if (nodes.find(m.second) != nodes.end()) {
                nodes[m.first] = nodes[m.second];
            }
        }
    }
    document["nodes"] = nodes;
    return document;
}

void mapNextTree(const Tree_t& summaryTree,
                 const map<OttId, const Tree_t::node_type*>& constSummaryOttIdToNode,
                 const Tree_t & tree,
                 const string& source_name) {
    typedef Tree_t::node_type node_type;
    std::function<const node_type*(const node_type*,const node_type*)> mrca_of_pair = [](const node_type* n1, const node_type* n2) {return mrca_from_depth(n1,n2);};
    auto ottid_to_node = get_ottid_to_const_node_map(tree);
    {
        auto log_supported_by    = [&](const node_t* node2, const node_t* node1) {set_supported_by(node2,node1,source_name);};
        auto log_partial_path_of = [&](const node_t* node2, const node_t* node1) {set_partial_path_of(node2,node1,source_name);};
        auto log_conflicts_with  = [&](const node_t* node2, const node_t* node1) {set_conflicts_with(node2,node1,source_name);};
        auto log_resolved_by     = [&](const node_t* node2, const node_t* node1) {set_resolved_by(node2,node1,source_name);};
        auto log_terminal        = [&](const node_t* node2, const node_t* node1) {set_terminal(node2,node1,source_name);};

        perform_conflict_analysis(tree, ottid_to_node, mrca_of_pair,
                                  summaryTree, constSummaryOttIdToNode, mrca_of_pair,
                                  log_supported_by,
                                  log_partial_path_of,
                                  log_conflicts_with,
                                  log_resolved_by,
                                  log_terminal);
    }
    {
        auto log_supported_by    = [&](const node_t*, const node_t*) {};
        auto log_partial_path_of = [&](const node_t*, const node_t*) {};
        auto log_conflicts_with  = [&](const node_t*, const node_t*) {};
        auto log_resolved_by     = [&](const node_t* node2, const node_t* node1) {set_resolves(node1,node2,source_name);};
        auto log_terminal        = [&](const node_t*, const node_t*) {};

        perform_conflict_analysis(summaryTree, constSummaryOttIdToNode, mrca_of_pair,
                                  tree, ottid_to_node, mrca_of_pair,
                                  log_supported_by,
                                  log_partial_path_of,
                                  log_conflicts_with,
                                  log_resolved_by,
                                  log_terminal);
    }
}


int main(int argc, char *argv[]) {
    try {
        variables_map args = parse_cmd_line(argc,argv);
        string synthfilename = args["synth"].as<string>();
        vector<string> inputs = args["input"].as<vector<string>>();
        bool ignore_monotypic = args.count("ignore-monotypic");
        // 1. Load and process summary tree.
            auto summaryTree = get_tree<Tree_t>(synthfilename);
        auto summaryOttIdToNode = get_ottid_to_node_map(*summaryTree);
        auto constSummaryOttIdToNode = get_ottid_to_const_node_map(*summaryTree);
        auto monotypic_nodes = suppress_and_record_monotypic(*summaryTree);
        compute_depth(*summaryTree);
        // 2. Load and process input trees.
        json sources;
        for(const auto& filename: inputs) {
            auto tree = get_tree<Tree_t>(filename);
            compute_depth(*tree);
            compute_summary_leaves(*tree, summaryOttIdToNode);
            string source_name = source_from_tree_name(tree->get_name());
            mapNextTree(*summaryTree, constSummaryOttIdToNode, *tree, source_name);
            sources.push_back(source_name);
        }
        // 3. Generate json document and print it.
        auto document = gen_json(*summaryTree, monotypic_nodes, ignore_monotypic);
        document["sources"] = sources;
        std::cout << document.dump(1) << std::endl;
    } catch (std::exception& e) {
        std::cerr << "otc-annotate-synth: Error! " << e.what() << std::endl;
        exit(1);
    }
}
