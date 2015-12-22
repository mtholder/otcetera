// See https://github.com/OpenTreeOfLife/opentree/wiki/Open-Tree-of-Life-APIs-v3#synthetic-tree
#include "otc/otcli.h"
#include "otc/supertree_util.h"
#include <tuple>
#include <sstream>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include "json.hpp"

using namespace otc;
using json = nlohmann::json;
using std::vector;
using std::string;
using std::map;
using std::pair;

// Add an instantiation of std::hash< > for pair<string,json> so that we can use unordered_map & unordered_set.
template<>
struct std::hash<std::pair<string,json>>
{
    std::size_t operator()(const std::pair<string,json>& p) const noexcept {return std::hash<string>()(p.first) * std::hash<json>()(p.second);}
};

// The node data: record the @depth to speed up MRCA queries.
//                @mark is used as a bitvector.
struct RTNodeDepth {
    int depth = 0; // depth = number of nodes to the root of the tree including the  endpoints (so depth of root = 1s)
    int mark = 0;
    RootedTreeNode<RTNodeDepth>* summary_node;
};

using Tree_t = RootedTree<RTNodeDepth, RTreeNoData>;
using node_t = Tree_t::node_type;

int depth(const Tree_t::node_type* node);
int& depth(Tree_t::node_type* node);
Tree_t::node_type* summary_node(const Tree_t::node_type* node);
Tree_t::node_type*& summary_node(Tree_t::node_type* node);
Tree_t::node_type* nmParent(Tree_t::node_type* node);
void computeDepth(Tree_t& tree);
void computeSummaryLeaves(Tree_t& tree, const map<long,Tree_t::node_type*>& summaryOttIdToNode);
string getSourceNodeNameFromNameOrOttId(const Tree_t::node_type* node);
Tree_t::node_type* trace_to_parent(Tree_t::node_type* node, int bits);
Tree_t::node_type* get_root(Tree_t::node_type* node);
const Tree_t::node_type* get_root(const Tree_t::node_type* node);
Tree_t::node_type* trace_find_MRCA(Tree_t::node_type* node1, Tree_t::node_type* node2, int bits1, int bits2);
Tree_t::node_type* trace_include_group_find_MRCA(const Tree_t::node_type* node, int bits);
Tree_t::node_type* trace_exclude_group_find_MRCA(const Tree_t::node_type* node, int bits1, int bits2);
void find_anc_conflicts(Tree_t::node_type* node, vector<Tree_t::node_type*>& conflicts);
void find_conflicts(const Tree_t& tree, vector<Tree_t::node_type*>& conflicts);
void trace_clean_marks(Tree_t::node_type* node);
void trace_clean_marks_from_synth(const Tree_t& tree);
json source_node(const Tree_t::node_type* input_node, const Tree_t& input_tree);

inline int depth(const Tree_t::node_type* node) {
    return node->getData().depth;
}


inline int& depth(Tree_t::node_type* node) {
    return node->getData().depth;
}

inline Tree_t::node_type* summary_node(const Tree_t::node_type* node) {
    return node->getData().summary_node;
}

inline Tree_t::node_type*& summary_node(Tree_t::node_type* node) {
    return node->getData().summary_node;
}

// returns the most recent non-monotypic ancestor of node (or nullptr if there is no such node)
inline Tree_t::node_type* nmParent(Tree_t::node_type* node) {
    do {
        node = node->getParent();
    } while (node and node->isOutDegreeOneNode());
    return node;
}

// fills in the depth data member for each node.
void computeDepth(Tree_t& tree) {
    tree.getRoot()->getData().depth = 1;
    for (auto nd: iter_pre(tree)) {
        if (nd->getParent()) {
            nd->getData().depth = nd->getParent()->getData().depth + 1;
        }
    }
}

// uses the OTT Ids in `tree` to fill in the `summary_node` field of each leaf
void computeSummaryLeaves(Tree_t& tree, const map<long,Tree_t::node_type*>& summaryOttIdToNode) {
    for(auto leaf: iter_leaf(tree)) {
        summary_node(leaf) = summaryOttIdToNode.at(leaf->getOttId());
    }
}


string getSourceNodeNameFromNameOrOttId(const Tree_t::node_type* node) {
    string name = node->getName();
    if (node->hasOttId()){
        name = "ott" + std::to_string(node->getOttId());
    }
    return getSourceNodeName(name);
}

// assumes `node` is marked with `bits`. Returns the parent of `node` and
//  also marks the parent with `bits` as a side effect.
Tree_t::node_type* trace_to_parent(Tree_t::node_type* node, int bits) {
    assert(is_marked(node, bits));
    node = node->getParent(); // move to parent and mark it.
    set_mark(node, bits);
    return node;
}

/// Find the root of a tree from a node
Tree_t::node_type* get_root(Tree_t::node_type* node) {
    while (node->getParent()) {
        node = node->getParent();
    }
    return node;
}

/// Find the root of a tree from a node
const Tree_t::node_type* get_root(const Tree_t::node_type* node)
{
    while (node->getParent()){
        node = node->getParent();
    }
    return node;
}

/// Walk up the tree from node1 and node2 until we find the common ancestor, marking all the way.
// for X=1,2 and Y = 3-X
//      assumes nodeX is marked by bitsX 
//      returns the MRCA, and guarantees that the path from MRCA to nodeX is marked with bitsX
//  if nodeX is nullptr, returns the other nodeY marked by bitsY
Tree_t::node_type* trace_find_MRCA(Tree_t::node_type* node1, Tree_t::node_type* node2, int bits1, int bits2) {
    assert(node1 or node2);
    if (not node1) {
        assert(is_marked(node2, bits2));
        return node2;
    }
    if (not node2) {
        assert(is_marked(node1, bits1));
        return node1;
    }
    assert(node1 and node2);
    assert(get_root(node1) == get_root(node2));
    assert(is_marked(node1, bits1));
    assert(is_marked(node2, bits2));
    while (depth(node1) > depth(node2)){
        node1 = trace_to_parent(node1, bits1);
    }
    while (depth(node1) < depth(node2)){
        node2 = trace_to_parent(node2, bits2);
    }
    assert(depth(node1) == depth(node2));
    while (node1 != node2) {
        assert(node1->getParent());
        assert(node2->getParent());
        node1 = trace_to_parent(node1, bits1);
        node2 = trace_to_parent(node2, bits2);
    }
    assert(node1 == node2);
    assert(is_marked(node1, bits1));
    assert(is_marked(node2, bits2));
    return node1;
}

// traces the summary nodes that correspond to the leaves of `node` back to their MRCA
//  on the summary tree. All of the nodes in this induced tree will be flagged by setting
//  `bits` to 1.
// returns the MRCA node (in the summary tree)
Tree_t::node_type* trace_include_group_find_MRCA(const Tree_t::node_type* node, int bits) {
    Tree_t::node_type* MRCA = nullptr;
    for (auto leaf: iter_leaf_n_const(*node)) {
        auto leaf2 = summary_node(leaf);
        mark(leaf2) |= bits;
        MRCA = trace_find_MRCA(MRCA, leaf2, bits, bits);
        assert(is_marked(MRCA, bits));
        if (MRCA->hasChildren()) {
            assert(countMarkedChildren(MRCA,bits)>0);
        }
    }
    return MRCA;
}


// Assumes that the version of the summary tree induced by the include group of `node` has
//  already been marked with `bits1`.
// Returns the MRCA of the exclude group, and assures that every node in the version of the
//  summary tree induced by the exclude group is flagged with `bits2` 
Tree_t::node_type* trace_exclude_group_find_MRCA(const Tree_t::node_type* node, int bits1, int bits2) {
    // Using bits1 to exclude leafs seems like a dumb way to iterate over excluded leaves.
    node = get_root(node);
    Tree_t::node_type* MRCA = nullptr;
    for(auto leaf: iter_leaf_n(*node)) {
        auto leaf2 = summary_node(leaf);
        if (!is_marked(leaf2, bits1)) {
            mark(leaf2) |= bits2;
            MRCA = trace_find_MRCA(MRCA, leaf2, bits2, bits2);
        }
    }
    return MRCA;
}

// Walks from `node` to the induced root (real root or the deepest node with some mark).
// adds the a node to `conflicts` if that node is marked by the include AND exclude bit
//  but the node is NOT the deepest node marked with the include flag.
// The latter (but...NOT) condition avoids flagging nodes that are polytomies which
//  could be resolved in favor of a node as "conflict" nodes.
// This is sub-optimal, because we walk each branch n times if it has n children.
// There a conflict will be discovered n times if it has n children.
// We currently hack around this by checking for duplicate entries in the hash.
void find_anc_conflicts(Tree_t::node_type* node, vector<Tree_t::node_type*>& conflicts) {
    while (node and mark(node)) {
        if (is_marked(node,1)
            and is_marked(node,2)
            and node->getParent()
            and is_marked(node->getParent(), 1)) {
            conflicts.push_back(node);
        }
        node = node->getParent();
    } 
}

// calls find_anc_conflicts for each leaf to fill `conflicts`
void find_conflicts(const Tree_t& tree, vector<Tree_t::node_type*>& conflicts) {
    conflicts.clear();
    for(auto leaf: iter_leaf_const(tree)) {
        auto leaf2 = summary_node(leaf);
        find_anc_conflicts(leaf2, conflicts);
    }
}


void trace_clean_marks(Tree_t::node_type* node) {
    while (node and mark(node)) {
        mark(node) = 0;
        node = node->getParent();
        // MTH should  be able to bail out here if the next node is already mark(node) == 0, right?
    } 
}

// sets marks to 0 for all nodes of the summary tree that are induced by the leaf set of tree.
void trace_clean_marks_from_synth(const Tree_t& tree) {
    for(auto leaf: iter_leaf_const(tree)) {
        auto leaf2 = summary_node(leaf);
        trace_clean_marks(leaf2);
    }
}

json source_node(const Tree_t::node_type* input_node, const Tree_t& input_tree) {
    string source = source_from_tree_name(input_tree.getName());
    string node_in_study = getSourceNodeNameFromNameOrOttId(input_node);
    return {source,node_in_study};
}

struct DisplayedStatsState : public TaxonomyDependentTreeProcessor<Tree_t> {
    json document;
    std::unique_ptr<Tree_t> summaryTree;
    std::map<long,const Tree_t::node_type*> taxOttIdToNode;
    std::map<long,Tree_t::node_type*> summaryOttIdToNode;
    std::unordered_multimap<string,json> supported_by;
    std::unordered_multimap<string,json> partial_path_of;
    std::unordered_multimap<string,json> conflicts_with;
    std::unordered_multimap<string,json> could_resolve;
    std::unordered_multimap<string,json> terminal;
    std::unordered_set<pair<string,json>> supported_by_set;
    std::unordered_set<pair<string,json>> partial_path_of_set;
    std::unordered_set<pair<string,json>> conflicts_with_set;
    std::unordered_set<pair<string,json>> could_resolve_set;
    std::unordered_set<pair<string,json>> terminal_set;
    int numErrors = 0;
    bool treatTaxonomyAsLastTree = false;
    bool conflictOnly = false;
    bool headerEmitted = false;
    bool suppressTerminals = false;

    virtual ~DisplayedStatsState(){}

    void set_terminal(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const Tree_t& input_tree)
    {
        auto x = std::pair<string,json>({synth_node->getName(), source_node(input_node,input_tree)});
        if (not terminal_set.count(x))
        {
            terminal_set.insert(x);
            terminal.insert(x);
        }
    }

    void set_supported_by(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const Tree_t& input_tree)
    {
        auto x = std::pair<string,json>({synth_node->getName(), source_node(input_node,input_tree)});
        if (not supported_by_set.count(x))
        {
            supported_by_set.insert(x);
            supported_by.insert(x);
        }
    }

    void set_partial_path_of(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const Tree_t& input_tree)
    {
        auto x = std::pair<string,json>({synth_node->getName(), source_node(input_node,input_tree)});
        if (not partial_path_of_set.count(x))
        {
            partial_path_of_set.insert(x);
            partial_path_of.insert(x);
        }
    }

    void set_conflicts_with(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const Tree_t& input_tree)
    {
        auto x = std::pair<string,json>({synth_node->getName(), source_node(input_node,input_tree)});
        if (not conflicts_with_set.count(x))
        {
            conflicts_with_set.insert(x);
            conflicts_with.insert(x);
        }
    }

    void set_could_resolve(const Tree_t::node_type* synth_node, const Tree_t::node_type* input_node, const Tree_t& input_tree)
    {
        auto x = std::pair<string,json>({synth_node->getName(), source_node(input_node,input_tree)});
        if (not could_resolve_set.count(x))
        {
            could_resolve_set.insert(x);
            could_resolve.insert(x);
        }
    }

    void reset() {
        document = {};

        supported_by.clear();
        partial_path_of.clear();
        conflicts_with.clear();
        could_resolve.clear();
        terminal.clear();

        supported_by_set.clear();
        partial_path_of_set.clear();
        conflicts_with_set.clear();
        could_resolve_set.clear();
        terminal_set.clear();
    }

    bool summarize(OTCLI &otCLI) override {
        return true;
    }
    bool report(OTCLI &otCLI) {
        if (treatTaxonomyAsLastTree) {
            mapNextTree(otCLI, *taxonomy, true);
        }

        document["num_tips"] = countLeaves(*summaryTree);
        document["root_ott_id"] = summaryTree->getRoot()->getOttId();

        json nodes;
        for(auto nd: iter_post_const(*summaryTree))
        {
            json node;
            string name = nd->getName();
            if (nd->hasOttId())
                name = "ott" + std::to_string(nd->getOttId());

            {
                json j_supported_by = json::array();

                auto range = supported_by.equal_range(name);
                for (auto iter = range.first; iter!=range.second; ++iter)
                    j_supported_by.push_back(iter->second);
                if (not j_supported_by.empty())
                    node["supported_by"] = j_supported_by;
            }
            if (not suppressTerminals)
            {
                json j_terminal = json::array();

                auto range = terminal.equal_range(name);
                for (auto iter = range.first; iter!=range.second; ++iter)
                    j_terminal.push_back(iter->second);
                if (not j_terminal.empty())
                    node["terminal"] = j_terminal;
            }
            {
                json j_partial_path_of = json::array();

                auto range = partial_path_of.equal_range(name);
                for (auto iter = range.first; iter!=range.second; ++iter)
                    j_partial_path_of.push_back(iter->second);
                if (not j_partial_path_of.empty())
                    node["partial_path_of"] = j_partial_path_of;
            }
            {
                json j_conflicts_with = json::array();

                auto range = conflicts_with.equal_range(name);
                for (auto iter = range.first; iter!=range.second; ++iter)
                    j_conflicts_with.push_back(iter->second);
                if (not j_conflicts_with.empty())
                    node["conflicts_with"] = j_conflicts_with;
            }
            {
                json j_could_resolve = json::array();

                auto range = could_resolve.equal_range(name);
                for (auto iter = range.first; iter!=range.second; ++iter)
                    j_could_resolve.push_back(iter->second);
                if (not j_could_resolve.empty())
                    node["could_resolve"] = j_could_resolve;
            }
            if (not node.empty())
                nodes[name] = node;
        }
        document["nodes"] = nodes;
        if (conflictOnly)
            document = document["nodes"];
        std::cout<<document.dump(4)<<std::endl;
        return true;
    }

    void mapNextTree(OTCLI & , Tree_t & tree, bool ) //isTaxoComp is third param
    {
        computeDepth(tree);
        computeSummaryNodes(tree, summaryOttIdToNode);

        vector<Tree_t::node_type*> conflicts;
        string source_name = source_from_tree_name(tree.getName());
        document["sources"].push_back(source_name);
        
        // How does each node of the input tree map to the synthesis tree?
        for(const auto nd: iter_post_const(tree))
        {
            if (not nd->getParent()) continue;

            // Trace the include group in the summary tree, marking with bit 1
            auto MRCA_include = trace_include_group_find_MRCA(nd, 1);
            assert(not is_marked(MRCA_include,2));
            // Trace the exclude group in the summary tree, marking with bit 2
            auto MRCA_exclude = trace_exclude_group_find_MRCA(nd, 1, 2);

            // Connect the two groups up to the MRCA of both include & exclude.
            // Since the root is in the exclude group, mark branch between the MRCA and the exclude MRCA with bit 2.
            MRCA_exclude = trace_find_MRCA(MRCA_include, MRCA_exclude, 0, 2);

            if (nd->isTip())
            {
                assert(mark(MRCA_include) == 1);
                for(auto path_node = MRCA_include;path_node and not is_marked(path_node,2);path_node = path_node->getParent())
                    set_terminal(path_node, nd, tree);
            }
            // include group separated from exclude group.
            else if (mark(MRCA_include) == 1)
            {
                if (MRCA_include->getParent() and mark(nmParent(MRCA_include)) == 2)
                {
                    auto path_node = MRCA_include;
                    do {
                        set_supported_by(path_node, nd, tree);
                        path_node = path_node->getParent();
                    } while(path_node->isOutDegreeOneNode());
                }
                else
                {
                    for(auto path_node = MRCA_include;path_node and not is_marked(path_node,2);path_node = path_node->getParent())
                        set_partial_path_of(path_node, nd, tree);
                }
            }
            assert(is_marked(MRCA_include,1));
            bool conflicts_or_could_resolve = is_marked(MRCA_include,2);

            find_conflicts(tree, conflicts);
            trace_clean_marks_from_synth(tree);
            
            if (nd->isTip() or mark(MRCA_include) == 1) assert(conflicts.empty());

            for(auto conflicting_node: conflicts)
                set_conflicts_with(conflicting_node, nd, tree);

            if (conflicts.empty() and conflicts_or_could_resolve)
                set_could_resolve(MRCA_include, nd, tree);

#ifdef CHECK_MARKS
            for(const auto nd2: iter_post_const(*summaryTree))
            {
                assert(mark(nd2) == 0);
            }
#endif
        }
    }

    virtual bool processTaxonomyTree(OTCLI & otCLI) override {
        TaxonomyDependentTreeProcessor<Tree_t>::processTaxonomyTree(otCLI);
        otCLI.getParsingRules().includeInternalNodesInDesIdSets = false;
        otCLI.getParsingRules().requireOttIds = false;
        for(auto nd: iter_post_const(*taxonomy))
            if (nd->hasOttId())
                taxOttIdToNode[nd->getOttId()] = nd;
        return true;
    }

    bool processSummaryTree(OTCLI & otCLI, std::unique_ptr<Tree_t> tree) {
        summaryTree = std::move(tree);
        for(auto nd: iter_post(*summaryTree))
            if (nd->hasOttId())
                summaryOttIdToNode[nd->getOttId()] = nd;
        return true;
    }

    bool processSourceTree(OTCLI & otCLI, std::unique_ptr<Tree_t> tree) override {
        computeDepth(*tree);
        assert(taxonomy != nullptr);
        if (summaryTree == nullptr)
            return processSummaryTree(otCLI, std::move(tree));
        requireTipsToBeMappedToTerminalTaxa(*tree, taxOttIdToNode);

        mapNextTree(otCLI, *tree, false);
        return true;
    }
};
bool handleCountTaxonomy(OTCLI & otCLI, const std::string &);

bool handleCountTaxonomy(OTCLI & otCLI, const std::string &) {
    DisplayedStatsState * proc = static_cast<DisplayedStatsState *>(otCLI.blob);
    assert(proc != nullptr);
    proc->treatTaxonomyAsLastTree = true;
    return true;
}

bool handleConflictOnly(OTCLI & otCLI, const std::string &) {
    DisplayedStatsState * proc = static_cast<DisplayedStatsState *>(otCLI.blob);
    assert(proc != nullptr);
    proc->conflictOnly = true;
    return true;
}

bool handleSuppressTerminals(OTCLI & otCLI, const std::string &) {
    DisplayedStatsState * proc = static_cast<DisplayedStatsState *>(otCLI.blob);
    assert(proc != nullptr);
    proc->suppressTerminals = true;
    return true;
}

// idea: Read from std::cin.
//       Read the taxonomy and full synthesis tree (eventually just the synth tree) to define the problem.
//       Each line contains a number of trees that are processed at once.
//       Before processing each line, the 'reset()' function is called.
//       After processing each line, the summarize command is called.

int main(int argc, char *argv[]) {
    std::string explanation{"takes a newick file paths for a full supertree.\n"
            "It reads then reads lines from standard input, where each line represents a set of input trees."};
    OTCLI otCLI("otc-annotate-synth",
                explanation.c_str(),
                "taxonomy.tre synth.tre < inp.tre");
    DisplayedStatsState proc;
    otCLI.addFlag('x',
                  "Automatically treat the taxonomy as an input in terms of supporting groups",
                  handleCountTaxonomy,
                  false);
    otCLI.addFlag('c',
                  "Just print conflict information.",
                  handleConflictOnly,
                  false);
    otCLI.addFlag('t',
                  "Don't print information about terminals.",
                  handleSuppressTerminals,
                  false);

    // Read in the taxonomy tree and synthetic tree, updating object 'proc'.
    taxDependentTreeProcessingMain(otCLI, argc, argv, proc, 2, true);

    // Event loop: treat each line we read as a list of study tree names.
    string line;
    while(getline(std::cin,line))
    {
        try {
            // Split the line into a series of filenames
            std::istringstream iss(line);
            vector<string> filenames;
            copy(std::istream_iterator<string>(iss),
                 std::istream_iterator<string>(),
                 back_inserter(filenames));

            //  Create anonymous function object to ingest trees we read in.
            std::function<bool(std::unique_ptr<Tree_t>)> analyze =
                [&proc,&otCLI](std::unique_ptr<Tree_t> t)
                {
                    proc.mapNextTree(otCLI,*t,false);
                    return true;
                };

            // Clear input trees, read in new trees for this input line, and generate a JSON report.
            proc.reset();
            for(const auto& filename : filenames)
                processTrees(filename, otCLI.getParsingRules(), analyze);
            proc.report(otCLI);
        }
        catch (std::exception & x) {
            json document;
            document["exception"] = x.what();
            std::cout<<document.dump(4)<<std::endl;
        }
    }
}
