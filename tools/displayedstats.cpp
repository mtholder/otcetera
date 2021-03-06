#include "otc/otcli.h"
#include "otc/supertree_util.h"
#include <tuple>
#include <sstream>
#include <cstring>
#include <unordered_map>

using namespace otc;

using std::string;

static bool showJSON = false;

/// Stat Calc declarations
enum NDSB {
    ROOT_BIT = 0x01,
    OUTDEGREE_ONE_BIT = 0x02,
    LEAF_BIT = 0x04,
    DISPLAYED_BIT = 0x08,
    COULD_RESOLVE_BIT = 0x10,
    INCOMPATIBLE_BIT = 0x20,
    ANC_ALL_BIT = 0x40,
    END_BIT = 0x80
};
enum NDSE {
    ROOT_NODE = NDSB::ROOT_BIT | NDSB::ANC_ALL_BIT , // root of graph and has out-degree>1
    FIRST_FORK = NDSB::ANC_ALL_BIT , // root of phylo info, but has redundant parent
    LEAF_NODE = NDSB::LEAF_BIT,
    FORKING_DISPLAYED = NDSB::DISPLAYED_BIT,
    FORKING_COULD_RESOLVE = NDSB::COULD_RESOLVE_BIT,
    FORKING_INCOMPATIBLE = NDSB::INCOMPATIBLE_BIT,
    REDUNDANT_DISPLAYED = NDSB::DISPLAYED_BIT | NDSB::OUTDEGREE_ONE_BIT,
    REDUNDANT_COULD_RESOLVE = NDSB::COULD_RESOLVE_BIT | NDSB::OUTDEGREE_ONE_BIT,
    REDUNDANT_INCOMPATIBLE = NDSB::INCOMPATIBLE_BIT | NDSB::OUTDEGREE_ONE_BIT,
    REDUNDANT_TERMINAL = NDSB::LEAF_BIT | NDSB::OUTDEGREE_ONE_BIT,
    // is and anc of FIRST_FORK, but not root of graph
    REDUNDANT_ROOT_ANC = NDSB::ANC_ALL_BIT | NDSB::OUTDEGREE_ONE_BIT,
    // Is root of graph, but anc of FIRST_FORK
    ROOT_REDUNDANT_ROOT_ANC = NDSB::ROOT_BIT | NDSB::ANC_ALL_BIT | NDSB::OUTDEGREE_ONE_BIT,
    //ROOT is a TIP
    DOT_TREE = NDSB::ROOT_BIT | NDSB::ANC_ALL_BIT | NDSB::LEAF_BIT,
    REDUNDANT_LINE_TREE = NDSB::ANC_ALL_BIT | NDSB::LEAF_BIT | NDSB::OUTDEGREE_ONE_BIT,
    ROOT_REDUNDANT_LINE_TREE = NDSB::ANC_ALL_BIT | NDSB::LEAF_BIT | NDSB::OUTDEGREE_ONE_BIT,
    END_VALUE = NDSB::END_BIT
};

inline NDSE operator|(NDSE f, NDSE s) {
    return static_cast<NDSE>(static_cast<int>(f) | static_cast<int>(s));
}
inline NDSE operator|(NDSB f, NDSE s) {
    return static_cast<NDSE>(static_cast<int>(f) | static_cast<int>(s));
}
inline NDSE operator|(NDSE f, NDSB s) {
    return static_cast<NDSE>(static_cast<int>(f) | static_cast<int>(s));
}

std::pair<NDSE, const NodeWithSplits *>
classifyInpNode(const TreeMappedWithSplits & summaryTree,
                     const NodeWithSplits * nd,
                     const OttIdSet & leaf_set,
                     const NodeWithSplits * startSummaryNd,
                     bool isTaxoComp=false);

std::map<NDSE, std::size_t> doStatCalc(const TreeMappedWithSplits & summaryTree,
                                       const TreeMappedWithSplits & inpTree,
                                       std::map<const NodeWithSplits *, NDSE> * node2Classification=nullptr,
                                       bool isTaxoComp=false);
/// end Stat Calc declarations.
/// end Stat Calc impl.

std::pair<NDSE, const NodeWithSplits *>
classifyInpNode(const TreeMappedWithSplits & summaryTree,
                     const NodeWithSplits * nd,
                     const OttIdSet & leaf_set,
                     const NodeWithSplits * startSummaryNd,
                     bool isTaxoComp) {
    using CN = std::pair<NDSE, const NodeWithSplits *>;
    const auto & ndi = nd->get_data().des_ids;
    assert(nd);
    if (ndi.size() == leaf_set.size()) {
        if (nd->get_parent() == nullptr) {
            if (nd->is_outdegree_one_node()) {
                if (ndi.size() == 1) {
                    return CN{NDSE::ROOT_REDUNDANT_LINE_TREE, nullptr};
                }
                return CN{NDSE::ROOT_REDUNDANT_ROOT_ANC, nullptr};
            }
            if (nd->is_tip()) {
                return CN{NDSE::DOT_TREE, nullptr};
            }
            return CN{NDSE::ROOT_NODE, nullptr};
        } else {
            if (nd->is_outdegree_one_node()) {
                if (ndi.size() == 1) {
                    return CN{NDSE::REDUNDANT_LINE_TREE, nullptr};
                }
                return CN{NDSE::REDUNDANT_ROOT_ANC, nullptr};
            }
            if (nd->is_tip()) {
                return CN{NDSE::LEAF_NODE, nullptr};
            }
            return CN{NDSE::FIRST_FORK, nullptr};
        }
    }
    assert(nd->get_parent() != nullptr);
    assert(ndi.size() > 1);
    if (startSummaryNd == nullptr) {
        const OttId firstID = *ndi.rbegin();
        startSummaryNd = summaryTree.get_data().get_node_by_ott_id(firstID);
        if (startSummaryNd == nullptr) {
            std::string m = "OTT id not found ";
            m += std::to_string(firstID);
            throw OTCError(m);
        }
    }
    if (nd->is_tip()) {
        return std::pair<NDSE, const NodeWithSplits *>{NDSE::LEAF_NODE, startSummaryNd};
    }
    
    const NodeWithSplits * rn = startSummaryNd;
    if (isTaxoComp) { // we are comparing against the taxonomy, so we know that the leafset is all other tips...
        for (;;) {
            const auto & sumdi = rn->get_data().des_ids;
            if (is_subset(ndi, sumdi)) {
                if (sumdi.size() == ndi.size()) {
                    if (nd->is_outdegree_one_node()) {
                        return CN{NDSE::REDUNDANT_DISPLAYED, rn};
                    }
                    return CN{NDSE::FORKING_DISPLAYED, rn};
                }
                if (can_be_resolved_to_display_only_inc_exc_group(rn, ndi)) {
                    if (nd->is_outdegree_one_node()) {
                        return CN{NDSE::REDUNDANT_COULD_RESOLVE, rn};
                    }
                    return CN{NDSE::FORKING_COULD_RESOLVE, rn};
                }
                break; // incompatible
            }
            if (!is_subset(sumdi, ndi)) {
                break; // incompatible
            }
            rn = rn->get_parent();
            if (rn == nullptr) {
                const auto z = set_difference_as_set(ndi, sumdi);
                auto x = *z.begin();
                std::string m = "OTT id not found ";
                m += std::to_string(x);
                throw OTCError(m);
            }
        }
    } else {
        for (;;) {
            const auto & sumdi = rn->get_data().des_ids;
            const auto extra = set_difference_as_set(sumdi, ndi);
            if (is_subset(ndi, sumdi)) {
                if (are_disjoint(extra, leaf_set)) {
                    if (nd->is_outdegree_one_node()) {
                        return CN{NDSE::REDUNDANT_DISPLAYED, rn};
                    }
                    return CN{NDSE::FORKING_DISPLAYED, rn};
                }
                const auto exc = set_difference_as_set(leaf_set, ndi);
                if (can_be_resolved_to_display_inc_exc_group(rn, ndi, exc)) {
                    if (nd->is_outdegree_one_node()) {
                        return CN{NDSE::REDUNDANT_COULD_RESOLVE, rn};
                    }
                    return CN{NDSE::FORKING_COULD_RESOLVE, rn};
                }
                break; // incompatible
            }
            if (!are_disjoint(extra, leaf_set)) {
                break; // incompatible
            }
            rn = rn->get_parent();
            if (rn == nullptr) {
                const auto z = set_difference_as_set(ndi, sumdi);
                auto x = *z.begin();
                std::string m = "OTT id not found ";
                m += std::to_string(x);
                throw OTCError(m);
            }
        }
    }
    if (nd->is_outdegree_one_node()) {
        return CN{NDSE::REDUNDANT_INCOMPATIBLE, rn};
    }
    return CN{NDSE::FORKING_INCOMPATIBLE, rn};
}


string quote(const string& s);

inline string quote(const string& s) {
    return '"'+s+'"';
}

std::map<NDSE, std::size_t> doStatCalc(const TreeMappedWithSplits & summaryTree,
                                       const TreeMappedWithSplits & inpTree,
                                       std::map<const NodeWithSplits *, NDSE> * node2Classification,
                                       std::unordered_multimap<string,string> * support,
                                       std::unordered_multimap<string,string> * conflict,
                                       bool isTaxoComp);

std::map<NDSE, std::size_t> doStatCalc(const TreeMappedWithSplits & summaryTree,
                                       const TreeMappedWithSplits & inpTree,
                                       std::map<const NodeWithSplits *, NDSE> * node2Classification,
                                       std::unordered_multimap<string,string> * support,
                                       std::unordered_multimap<string,string> * conflict,
                                       bool isTaxoComp) {
    std::map<NDSE, std::size_t> r;
    if (inpTree.get_root() == nullptr) {
        return r;
    }
    std::map<const NodeWithSplits *, NDSE> localNd2C;
    std::map<const NodeWithSplits *, NDSE> & nd2t{node2Classification == nullptr ? localNd2C : *node2Classification};
    std::map<const NodeWithSplits *, const NodeWithSplits *> nd2summaryTree;
    const auto & treeLeafSet = inpTree.get_root()->get_data().des_ids;
    for (auto nd : iter_post_const(inpTree)) {
        NDSE t = NDSE::END_VALUE;
        if (nd->is_tip()) {
            if (nd->get_parent() != nullptr) {
                t = NDSE::LEAF_NODE;
            } else {
                t = NDSE::DOT_TREE;
            }
        } else if (nd->get_parent() == nullptr) {
            if (nd->is_tip()) {
                t = NDSE::DOT_TREE;
            } else if (nd->is_outdegree_one_node()) {
                t = NDSE::ROOT_REDUNDANT_ROOT_ANC;
            } else {
                t = NDSE::ROOT_NODE;
            }
        } else if (nd->is_outdegree_one_node()) {
            auto child = nd->get_first_child();
            const NDSE ct = nd2t[child];
            auto ns = nd2summaryTree.find(child);
            if (ns != nd2summaryTree.end()) {
                nd2summaryTree[nd] = ns->second;
                nd2summaryTree.erase(ns); // we won't need this child mapping again
            }
            assert(ct != NDSE::ROOT_NODE
                   && ct != NDSE::ROOT_REDUNDANT_ROOT_ANC
                   && ct != NDSE::DOT_TREE);
            t = NDSB::OUTDEGREE_ONE_BIT | ct;
        } else {
            const NodeWithSplits * startSummaryNd = nullptr;
            for (auto c : iter_child_const(*nd)) {
                auto x = nd2summaryTree.find(c);
                if (x != nd2summaryTree.end()) {
                    startSummaryNd = x->second;
                    nd2summaryTree.erase(c);
                    break;
                }
            }
            auto p = classifyInpNode(summaryTree, nd, treeLeafSet, startSummaryNd, isTaxoComp);
            t = p.first;
            if (p.second != nullptr) {
                nd2summaryTree[nd] = p.second;
                if (p.first == NDSE::FORKING_DISPLAYED and support) {
                    string node = p.second->get_name();
                    if (p.second->has_ott_id()) {
                        node = "ott"+std::to_string(p.second->get_ott_id());
                    }
                    string study = quote(study_from_tree_name(inpTree.get_name()));
                    string tree_in_study = quote(tree_in_study_from_tree_name(inpTree.get_name()));
                    string node_in_study = quote(*get_source_node_name(nd->get_name()));
                    std::ostringstream study_tree_node;;
                    study_tree_node << "[" << study << ", " << tree_in_study << ", " << node_in_study << "]";
                    support->insert({node, study_tree_node.str()});
                } else if (p.first == NDSE::FORKING_INCOMPATIBLE and conflict) {
                    string node = p.second->get_name();
                    if (p.second->has_ott_id()) {
                        node = "ott"+std::to_string(p.second->get_ott_id());
                    }
                    string study = quote(study_from_tree_name(inpTree.get_name()));
                    string tree_in_study = quote(tree_in_study_from_tree_name(inpTree.get_name()));
                    string node_in_study = quote(*get_source_node_name(nd->get_name()));
                    std::ostringstream study_tree_node;;
                    study_tree_node << "[" << study << ", " << tree_in_study << ", " << node_in_study << "]";
                    conflict->insert({node, study_tree_node.str()});
                }
            }
        }
        assert(t != END_VALUE);
        r[t] += 1;
        nd2t[nd] = t;
    }
    return r;
}
/// end Stat Calc impl
/// Stat report decl
void writeHeader(std::ostream &out);
void writeRow(std::ostream &out, std::map<NDSE, std::size_t> & m, const std::string & label);
std::string explainOutput();
/// End Stat report decl
/// Stat report impl
std::string explainOutput() {
    std::ostringstream o;
    o << "Writes tab-separated output.\n";
    o << "Each row reports the number of internal nodes of the input tree that fall into each category.\n";
    o << "The final row shows the totals.\n";
    o << "The two \"axes\" that the statistics explore are support and out-degree.\n";
    o << "Columns starting with \"F\" are \"forking\" internal nodes with out-degree > 1.\n";
    o << "Columns starting with \"R\" are \"redundant\" internal nodes with out-degree = 1.\n";
    o << "A \"D\" suffix to a column header means that the node is displayed by the summary tree.\n";
    o << "A \"CR\" suffix means that the node is could resolve a polytomy in the summary tree (so the\n";
    o << "    summary tree is not unambiguously in conflict in the node).\n";
    o << "An \"I\" suffix to a column header means that the node is incompatible with every resolution ofthe summary tree.\n";
    o << "For the redundant nodes, the report indicates the conflict status of their closest non-redundant descendant.\n";
    o << "A redundant node can also be marked \"T\" (for \"trivial\")if it is an ancestor of only 1 leaf or of the root.\n";
    o << "The \"F\" and \"R\" column are just the sums for forking and redundant entries.\n";
    o << "The \"label\" shows the tree name or \"Total of # trees\" for the global sum\n";
    o << "The ordering of the rows is the input order. For columns it is:\n";
    o <<  "FD FCR FI F RD RCR RI RT R label";
    return o.str();
}

void writeHeader(std::ostream &out) {
    out << "FD" << '\t'
        << "FCR" << '\t'
        << "FI" << '\t'
        << "F" << '\t'
        << "RD" << '\t'
        << "RCR"  << '\t'
        << "RI" << '\t'
        << "RT" << '\t'
        << "R" << '\t'
        << "label" << '\n';
}


void writeRow(std::ostream &out,
              std::map<NDSE, std::size_t> & m,
              const std::string & label) {
    const auto f = m[NDSE::FORKING_DISPLAYED] + m[NDSE::FORKING_COULD_RESOLVE] + m[NDSE::FORKING_INCOMPATIBLE];
    const auto rt = m[NDSE::REDUNDANT_TERMINAL] + m[NDSE::REDUNDANT_ROOT_ANC];
    const auto r = m[NDSE::REDUNDANT_DISPLAYED] + m[NDSE::REDUNDANT_COULD_RESOLVE] + m[NDSE::REDUNDANT_INCOMPATIBLE] + rt;
    out << m[NDSE::FORKING_DISPLAYED] << '\t'
        << m[NDSE::FORKING_COULD_RESOLVE]  << '\t'
        << m[NDSE::FORKING_INCOMPATIBLE] << '\t'
        << f << '\t'
        << m[NDSE::REDUNDANT_DISPLAYED] << '\t'
        << m[NDSE::REDUNDANT_COULD_RESOLVE] << '\t'
        << m[NDSE::REDUNDANT_INCOMPATIBLE] << '\t'
        << rt << '\t'
        << r  << '\t'
        << label << '\n';
}

struct DisplayedStatsState : public TaxonomyDependentTreeProcessor<TreeMappedWithSplits> {
    std::unique_ptr<TreeMappedWithSplits> summaryTree;
    std::map<NDSE, std::size_t> totals;
    std::unordered_multimap<string,string> support;
    std::unordered_multimap<string,string> conflict;
    int numErrors = 0;
    bool treatTaxonomyAsLastTree = false;
    bool headerEmitted = false;
    int num_trees = 0;
    virtual ~DisplayedStatsState(){}

    bool summarize(OTCLI &otCLI) override {
        if (treatTaxonomyAsLastTree) {
            statsForNextTree(otCLI, *taxonomy, true);
        }
        if (not showJSON) {
            const std::string label = std::string("Total of ") + std::to_string(num_trees) + std::string(" trees");
            writeNextRow(otCLI.out, totals, label);
        } else {
            std::cout << "  \"nodes\": {\n";
            for(auto nd: iter_post_const(*summaryTree)) {
                string name = nd->get_name();
                if (nd->has_ott_id()) {
                    name = "ott" + std::to_string(nd->get_ott_id());
                }
                const auto sc = support.count(name);
                const auto cc = conflict.count(name);
                if (sc + cc == 0) {
                    continue;
                }
                std::cout <<"    " <<quote(name) << ": { \n";
                if (sc) {
                    std::cout << "      \"supported-by\": [ ";
                    auto range = support.equal_range(name);
                    auto start = range.first;
                    auto end   = range.second;
                    for (auto iter = start; iter!=end; ++iter) {
                        if (iter != start) {
                            std::cout << "                        ";
                        }
                        std::cout << iter->second;
                        auto next = iter; ++next;
                        if (next != end) {
                            std::cout << ",\n";
                        }
                    }
                    std::cout << " ]";
                    if (cc > 0) {
                        std::cout << ",";
                    }
                    std::cout << "\n";
                }
                if (cc) {
                    std::cout << "      \"conflicts-with\": [ ";
                    auto range = conflict.equal_range(name);
                    auto start = range.first;
                    auto end   = range.second;
                    for (auto iter = start; iter!=end; ++iter) {
                        if (iter != start) {
                            std::cout << "                        ";
                        }
                        std::cout << iter->second;
                        auto next = iter; ++next;
                        if (next != end){
                            std::cout << ",\n";
                        }
                    }
                    std::cout << " ]\n";
                }
                std::cout << "    }\n";
            }
            std::cout << "  }\n";
        }
        return true;
    }

    void writeNextRow(std::ostream &out,
                      std::map<NDSE, std::size_t> & m,
                      const std::string & label) {
        if (!headerEmitted) {
            writeHeader(out);
            headerEmitted = true;
        }
        writeRow(out, m, label);
    }

    void statsForNextTree(OTCLI & otCLI, const TreeMappedWithSplits & tree, bool isTaxoComp) {
        auto c = doStatCalc(*summaryTree, tree, nullptr, showJSON?(&support):nullptr, showJSON?(&conflict):nullptr, isTaxoComp);
        if (not showJSON) writeNextRow(otCLI.out, c, tree.get_name());
        for (const auto & p : c) {
            totals[p.first] += p.second;
        }
        num_trees += 1;
    }

    virtual bool process_taxonomy_tree(OTCLI & otCLI) override {
        TaxonomyDependentTreeProcessor<TreeMappedWithSplits>::process_taxonomy_tree(otCLI);
        otCLI.get_parsing_rules().include_internal_nodes_in_des_id_sets = false;
        otCLI.get_parsing_rules().require_ott_ids = false;
        // now we get a little cute and reprocess the taxonomy des_ids so that they 
        // exclude internals. So that when we expand source trees, we expand just
        // to the taxonomy's leaf set
        clear_and_fill_des_ids(*taxonomy);
        return true;
    }

    bool process_source_tree(OTCLI & otCLI, std::unique_ptr<TreeMappedWithSplits> tree) override {
        assert(taxonomy != nullptr);
        if (summaryTree == nullptr) {
            summaryTree = std::move(tree);
            return true;
        }
        require_tips_to_be_mapped_to_terminal_taxa(*tree, *taxonomy);
        clear_and_fill_des_ids(*tree);
        statsForNextTree(otCLI, *tree, false);
        return true;
    }

};

bool handleCountTaxonomy(OTCLI & otCLI, const std::string &);
bool handleJSON(OTCLI & , const std::string &);

bool handleCountTaxonomy(OTCLI & otCLI, const std::string &) {
    DisplayedStatsState * proc = static_cast<DisplayedStatsState *>(otCLI.blob);
    assert(proc != nullptr);
    proc->treatTaxonomyAsLastTree = true;
    return true;
}

bool handleJSON(OTCLI & , const std::string &) {
    showJSON = true;
    return true;
}

int main(int argc, char *argv[]) {
    std::string explanation{"takes at least 2 newick file paths: a taxonomy,  a full supertree, and some number of input trees.\n"};
    explanation += explainOutput();
    OTCLI otCLI("otc-displayed-stats",
                explanation.c_str(),
                "synth.tre inp1.tre inp2.tre ...");
    DisplayedStatsState proc;
    otCLI.add_flag('x',
                  "Automatically treat the taxonomy as an input in terms of supporting groups",
                  handleCountTaxonomy,
                  false);
    otCLI.add_flag('j',
                  "Output JSON for node support, instead of displaying statistics.",
                  handleJSON,
                  false);
    return tax_dependent_tree_processing_main(otCLI, argc, argv, proc, 2, true);
}
