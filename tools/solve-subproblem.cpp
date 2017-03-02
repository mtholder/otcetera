#include <algorithm>
#include <set>
#include <list>
#include <iterator>

#include "otc/otcli.h"
#include "otc/tree_operations.h"
#include "otc/supertree_util.h"
#include "otc/tree_iter.h"
#include <fstream>

using namespace otc;
using std::vector;
using std::unique_ptr;
using std::set;
using std::list;
using std::map;
using std::string;
using namespace otc;

typedef TreeMappedWithSplits Tree_t;

int depth(const Tree_t::node_type* nd)
{
    return nd->get_data().depth;
}

/// Create a SORTED vector from a set
template <typename T>
vector<T> set_to_vector(const set<T>& s) {
    vector<T> v;
    v.reserve(s.size());
    std::copy(s.begin(), s.end(), std::back_inserter(v));
    return v;
}

struct RSplit {
    vector<int> in;
    vector<int> out;
    vector<int> all;
    RSplit() = default;
    RSplit(const set<int>& i, const set<int>& a)
        {
            in  = set_to_vector(i);
            all = set_to_vector(a);
            set_difference(begin(all), end(all), begin(in), end(in), std::inserter(out, out.end()));
            assert(in.size() + out.size() == all.size());
        }
};

RSplit split_from_include_exclude(const set<int>& i, const set<int>& e)
{
    RSplit s;
    s.in = set_to_vector(i);
    s.out = set_to_vector(e);
    set_union(begin(i),end(i),begin(e),end(e),std::inserter(s.all,s.all.end()));
    return s;
}

std::ostream& operator<<(std::ostream& o, const RSplit& s);
int merge_components(int c1, int c2, vector<int>& component, vector<list<int>>& elements);
bool empty_intersection(const set<int>& xs, const vector<int>& ys);
unique_ptr<Tree_t> BUILD(const vector<int>& tips, const vector<const RSplit*>& splits);
unique_ptr<Tree_t> BUILD(const vector<int>& tips, const vector<RSplit>& splits);
void add_names(Tree_t& tree, const vector<Tree_t::node_type const*>& taxa);
set<int> remap_ids(const set<long>& s1, const map<long,int>& id_map);
unique_ptr<Tree_t> combine(const vector<unique_ptr<Tree_t> >& trees, const set<long>&, bool verbose);
unique_ptr<Tree_t> make_unresolved_tree(const vector<unique_ptr<Tree_t>>& trees, bool use_ids);

namespace po = boost::program_options;
using po::variables_map;

variables_map parse_cmd_line(int argc,char* argv[]) 
{ 
    using namespace po;

    // named options
    options_description invisible("Invisible options");
    invisible.add_options()
        ("subproblem", value<vector<string>>()->composing(),"File containing ordered subproblem trees.")
        ;

    options_description output("Standard options");
    output.add_options()
	("incertae-sedis,I",value<string>(),"File containing Incertae sedis ids")
        ("root-name,n",value<string>(), "Rename the root to this name")
        ("no-higher-tips,l", "Tips may be internal nodes on the taxonomy.")
        ("prune-unrecognized,p","Prune unrecognized tips");
    
    options_description other("Other options");
    other.add_options()
        ("synthesize-taxonomy,T","Make unresolved taxonomy from input tips.")
        ("allow-no-ids,a", "Allow problems w/o OTT ids")
        ("standardize,S", "Write out a standardized subproblem and exit.")
        ;

    options_description visible;
    visible.add(output).add(other).add(otc::standard_options());

    // positional options
    positional_options_description p;
    p.add("subproblem", -1);

    variables_map vm = otc::parse_cmd_line_standard(argc, argv,
                                                    "Usage: otc-solve-subproblem <trees-file1> [<trees-file2> ... ] [OPTIONS]\n"
                                                    "Takes a series of tree files.\n"
						    "Files are concatenated and the combined list treated as a single subproblem.\n"
						    "Trees should occur in order of priority, with the taxonomy last.",
                                                    visible, invisible, p);
    return vm;
}

std::ostream& operator<<(std::ostream& o, const RSplit& s) {
    writeSeparatedCollection(o, s.in, " ") <<" | ";
    if (s.out.size() < 100) {
        writeSeparatedCollection(o, s.out, " ");
    } else {
        auto it = s.out.begin();
        for(int i=0;i<100;i++) {
            o << *it++ <<" ";
        }
        o << "...";
    }
    return o;
}

/// Merge components c1 and c2 and return the component name that survived
int merge_components(int ic1, int ic2, vector<int>& component, vector<list<int>>& elements) {
    std::size_t c1 = static_cast<std::size_t>(ic1);
    std::size_t c2 = static_cast<std::size_t>(ic2);
    if (elements[c2].size() > elements[c1].size()) {
        std::swap(c1, c2);
    }
    for(int i: elements[c2]) {
        component[static_cast<std::size_t>(i)] = static_cast<int>(c1);
    }
    elements[c1].splice(elements[c1].end(), elements[c2]);
    return static_cast<int>(c1);
}

bool empty_intersection(const set<int>& xs, const vector<int>& ys) {
    for (int y: ys){
        if (xs.count(y)) {
            return false;
        }
    }
    return true;
}

static vector<int> indices;

/// Construct a tree with all the splits mentioned, and return a null pointer if this is not possible
unique_ptr<Tree_t> BUILD(const vector<int>& tips, const vector<const RSplit*>& splits) {
#pragma clang diagnostic ignored  "-Wsign-conversion"
#pragma clang diagnostic ignored  "-Wsign-compare"
#pragma clang diagnostic ignored  "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored  "-Wsign-compare"
    std::unique_ptr<Tree_t> tree(new Tree_t());
    tree->create_root();
    // 1. First handle trees of size 1 and 2
    if (tips.size() == 1) {
        tree->getRoot()->setOttId(*tips.begin());
        return tree;
    } else if (tips.size() == 2) {
        auto Node1a = tree->create_child(tree->getRoot());
        auto Node1b = tree->create_child(tree->getRoot());
        auto it = tips.begin();
        Node1a->setOttId(*it++);
        Node1b->setOttId(*it++);
        return tree;
    }
    // 2. Initialize the mapping from elements to components
    vector<int> component;       // element index  -> component
    vector<list<int> > elements;  // component -> element indices
    for (int i=0;i<tips.size();i++) {
        indices[tips[i]] = i;
        component.push_back(i);
        elements.push_back({i});
    }
    // 3. For each split, all the leaves in the include group must be in the same component
    for(const auto& split: splits) {
        int c1 = -1;
        for(int i: split->in) {
            int j = indices[i];
            int c2 = component[j];
            if (c1 != -1 and c1 != c2) {
                merge_components(c1,c2,component,elements);
            }
            c1 = component[j];
        }
    }
    // 4. If we can't subdivide the leaves in any way, then the splits are not consistent, so return failure
    if (elements[component[0]].size() == tips.size()) {
        return {};
    }
    // 5. Make a vector of labels for the partition components
    vector<int> component_labels;                           // index -> component label
    vector<int> component_label_to_index(tips.size(),-1);   // component label -> index
    for (int c=0;c<tips.size();c++) {
        if (c == component[c]) {
            int index = component_labels.size();
            component_labels.push_back(c);
            component_label_to_index[c] = index;
        }
    }
    // 6. Create the vector of tips in each connected component 
    vector<vector<int>> subtips(component_labels.size());
    for(int i=0;i<component_labels.size();i++) {
        vector<int>& s = subtips[i];
        int c = component_labels[i];
        for (int j: elements[c]) {
            s.push_back(tips[j]);
        }
    }
    // 7. Determine the splits that are not satisfied yet and go into each component
    vector<vector<const RSplit*>> subsplits(component_labels.size());
    for(const auto& split: splits) {
        int first = indices[*split->in.begin()];
        assert(first >= 0);
        int c = component[first];
        // if none of the exclude group are in the component, then the split is satisfied by the top-level partition.
        bool satisfied = true;
        for(int x: split->out){
            if (indices[x] != -1 and component[indices[x]] == c) {
                satisfied = false;
                break;
            }
        }
        if (not satisfied) {
            int i = component_label_to_index[c];
            subsplits[i].push_back(split);
        }
    }
    // 8. Clear our map from id -> index, for use by subproblems.
    for(int id: tips) {
        indices[id] = -1;
    }
    // 9. Recursively solve the sub-problems of the partition components
    for(int i=0;i<subtips.size();i++) {
        auto subtree = BUILD(subtips[i], subsplits[i]);
        if (not subtree) {
            return {};
        }
        addSubtree(tree->getRoot(), *subtree);
    }
    return tree;
}

unique_ptr<Tree_t> BUILD(const vector<int>& tips, const vector<RSplit>& splits) {
    vector<const RSplit*> split_ptrs;
    for(const auto& split: splits) {
        split_ptrs.push_back(&split);
    }
    return BUILD(tips, split_ptrs);
}

template <typename T>
bool is_subset(const std::set<T>& set_two, const std::set<T>& set_one)
{
    return std::includes(set_one.begin(), set_one.end(), set_two.begin(), set_two.end());
}

Tree_t::node_type* add_monotypic_parent(Tree_t& tree, Tree_t::node_type* nd)
{
    if (nd->getParent())
    {
	auto p = nd->getParent();
	auto monotypic = tree.create_child(p);
	nd->detachThisNode();
	monotypic->addChild(nd);
	return monotypic;
    }
    else
    {
	auto monotypic = tree.create_root();
	monotypic->addChild(nd);
	return monotypic;
    }
}

void add_root_and_tip_names(Tree_t& summary, Tree_t& taxonomy)
{
    // name root
    summary.getRoot()->setName(taxonomy.getRoot()->get_name());

    // name tips
    auto summaryOttIdToNode = get_ottid_to_node_map(summary);

    for(auto nd: iter_leaf_const(taxonomy))
    {
	auto id = nd->getOttId();
	auto nd2 = summaryOttIdToNode.at(id);
	nd2->setName( nd->get_name());
    }
}

Tree_t::node_type* find_mrca_of_desids(const set<long>& ids, const std::unordered_map<long, Tree_t::node_type*>& summaryOttIdToNode)
{
    int first = *ids.begin();
    auto node = summaryOttIdToNode.at(first);
    while( not is_subset(ids, node->get_data().desIds) )
	node = node->getParent();
    return node;
}

bool is_ancestor_of(const Tree_t::node_type* n1, const Tree_t::node_type* n2)
{
    // make sure the depth fields are initialized
    assert(n1 == n2 or depth(n1) != 0 or depth(n2) != 0);

    if (depth(n2) > depth(n1))
    {
	while (depth(n2) != depth(n1))
	    n2 = n2->getParent();
	return (n2 == n1);
    }
    else
	return false;
}


const Tree_t::node_type* find_unique_maximum(const vector<const Tree_t::node_type*>& nodes)
{
    for(int i=0;i<nodes.size();i++)
    {
	bool is_ancestor = true;
	for(int j=0;j<nodes.size() and is_ancestor;j++)
	{
	    if (j==i) continue;
	    if (not is_ancestor_of(nodes[i],nodes[j])) is_ancestor = false;
	}
	if (is_ancestor)
	    return nodes[i];
    }
    return nullptr;
}

const Tree_t::node_type* select_canonical_ottid(const vector<const Tree_t::node_type*>& names)
{
    // We should only have to make this choice if there are at least 2 names to choose from.
    assert(names.size() >= 2);

    // Do something more intelligent here - perhaps prefer non-incertae-sedis taxa, and then choose lowest ottid.
    return names.front();
}

void register_ottid_equivalences(const Tree_t::node_type* canonical, const vector<const Tree_t::node_type*>& names)
{
    // First pass - actually we should write on a JSON file.
    std::cerr<<canonical->get_name()<<" (canonical): equivalent to ";
    for(auto name: names)
	std::cerr<<name->get_name()<<" ";
    std::cerr<<"\n";
}


/// Copy node names from taxonomy to tree based on ott ids, and copy the root name also
//  For this function, the taxa is a list of taxa that are displayed by the tree.
//  It is possible for different names to get assigned to the same node,Therefore, we need only 
void add_names(Tree_t& summary, const vector<const Tree_t::node_type*>& taxa)
{
    auto summaryOttIdToNode = get_ottid_to_node_map(summary);

    // 1. Set the desIds for the summary
    clearAndfillDesIdSets(summary);

    // 2. Associate each summary node with all the taxon nodes with that map to it.
    map<Tree_t::node_type*, vector<const Tree_t::node_type*>> name_groups;
    for(auto n2: taxa)
    {
	auto mrca = find_mrca_of_desids(n2->get_data().desIds, summaryOttIdToNode);

	if (not name_groups.count(mrca))
	    name_groups[mrca] = {};

	name_groups[mrca].push_back(n2);
    }

    // 3. Handle each summary 
    for(auto& name_group: name_groups)
    {
	auto summary_node = name_group.first;
	auto& names = name_group.second;

	// 3.1. As long as there is a unique root-most name, put that name in a monotypic parent.
	// This can occur when a node has two children, and one of them is an incertae sedis taxon that is moved more tip-ward.
	while (auto max = find_unique_maximum(names))
	{
	    if (names.size() == 1)
		summary_node->setName(max->get_name());
	    else
	    {
		auto p = add_monotypic_parent(summary, summary_node);
		p->setName(max->get_name());
		p->get_data().desIds = p->getFirstChild()->get_data().desIds;
	    }
	    names.erase(std::remove(names.begin(), names.end(), max), names.end());
	}

	// 3.2. Select a canonical name from the remaining names.
	if (not names.empty())
	{
	    // Select a specific ottid as the canonical name for this summary node
	    auto canonical = select_canonical_ottid(names);
	    summary_node->setName(canonical->get_name());

	    // Write out the equivalence of the remaining ottids to the canonical ottid
	    names.erase(std::remove(names.begin(), names.end(), canonical), names.end());
	    register_ottid_equivalences(canonical, names);
	}
    }
}

set<int> remap_ids(const set<long>& s1, const map<long,int>& id_map) {
    set<int> s2;
    for(auto x: s1) {
        auto it = id_map.find(x);
        assert(it != id_map.end());
        s2.insert(it->second);
    }
    return s2;
}

template <typename Tree_t>
vector<typename Tree_t::node_type const*> get_siblings(typename Tree_t::node_type const* nd)
{
    vector<typename Tree_t::node_type const*> sibs;
    for(auto sib = nd->getFirstSib(); sib; sib = sib->getNextSib()) {
	if (sib != nd) {
	    sibs.push_back(sib);
	}
    }
    return sibs;
}

template<typename Tree_T>
map<typename Tree_t::node_type const*, set<long>> construct_exclude_sets(const Tree_t& tree, const set<long>& incertae_sedis)
{
    map<typename Tree_t::node_type const*, set<long>> exclude;

    // Set exclude set for root node to the empty set.
    exclude[tree.getRoot()]; 	    

    for(auto nd: iter_pre_const(tree)) {

	if (nd->isTip()) continue;

	if (nd == tree.getRoot()) continue;
	
	// the exclude set contain the EXCLUDE set of the parent, plus the INCLUDE set of non-I.S. siblings
	set<long> ex = exclude.at(nd->getParent());
	for(auto nd2: get_siblings<Tree_t>(nd)) {
	    if (not incertae_sedis.count(nd2->getOttId())) {
		auto& ex_sib = nd2->get_data().desIds;
		ex.insert(begin(ex_sib),end(ex_sib));
	    }
	}
	exclude[nd] = ex;
    }
    return exclude;
}

/// Get the list of splits, and add them one at a time if they are consistent with previous splits
unique_ptr<Tree_t> combine(const vector<unique_ptr<Tree_t>>& trees, const set<long>& incertae_sedis, bool verbose) {
    // 0. Standardize names to 0..n-1 for this subproblem
    const auto& taxonomy = trees.back();
    auto all_leaves = taxonomy->getRoot()->get_data().desIds;
    // index -> id
    vector<long> ids;
    // id -> index
    map<long,int> id_map;
    for(long id: all_leaves) {
        int i = ids.size();
        id_map[id] = i;
        ids.push_back(id);
        assert(id_map[ids[i]] == i);
        assert(ids[id_map[id]] == id);
    }
    auto remap = [&id_map](const set<long>& argIds) {return remap_ids(argIds, id_map);};
    vector<int> all_leaves_indices;
    for(int i=0;i<all_leaves.size();i++) {
        all_leaves_indices.push_back(i);
    }
    indices.resize(all_leaves.size());
    for(auto& i: indices) {
        i=-1;
    }

    /// Incrementally add splits from @splits_to_try to @consistent if they are consistent with it.
    vector<RSplit> consistent;
    auto add_split_if_consistent = [&all_leaves_indices,verbose,&consistent](auto nd, RSplit&& split)
	{
	    consistent.push_back(std::move(split));

	    auto result = BUILD(all_leaves_indices, consistent);
	    if (not result) {
		consistent.pop_back();
		if (verbose and nd->hasOttId()) {
		    LOG(INFO) << "Reject: ott" << nd->getOttId() << "\n";
		}
		return false;
	    } else if (verbose and nd->hasOttId()) {
		LOG(INFO) << "Keep: ott" << nd->getOttId() << "\n";
	    }
	    return true;
	};


    // 1. Find splits in order of input trees
    vector<Tree_t::node_type const*> taxa;
    for(int i=0;i<trees.size();i++)
    {
	const auto& tree = trees[i];

        auto root = tree->getRoot();
        const auto leafTaxa = root->get_data().desIds;
        const auto leafTaxaIndices = remap(leafTaxa);

#ifndef NDEBUG
#pragma clang diagnostic ignored  "-Wunreachable-code-loop-increment"
        for(const auto& leaf: set_difference_as_set(leafTaxa, all_leaves)) {
            throw OTCError()<<"OTT Id "<<leaf<<" not in taxonomy!";
        }
#endif

	// Handle the taxonomy tree specially when it has Incertae sedis taxa.
	if (i == trees.size()-1 and not incertae_sedis.empty()) {
	    auto exclude = construct_exclude_sets<Tree_t>(*tree, incertae_sedis);

	    for(auto nd: iter_post_const(*tree)) {
		if (not nd->isTip() and nd != root) {
		    // construct split
		    const auto descendants = remap(nd->get_data().desIds);
		    const auto nondescendants = remap(exclude[nd]);

		    if (add_split_if_consistent(nd, split_from_include_exclude(descendants, nondescendants) ))
			taxa.push_back(nd);
		}
	    }
	}
	else if (i == trees.size()-1)
	{
	    for(auto nd: iter_post_const(*tree)) {
		if (not nd->isTip() and nd != root) {
		    const auto descendants = remap(nd->get_data().desIds);

		    if (add_split_if_consistent(nd, RSplit{descendants, leafTaxaIndices}))
			taxa.push_back(nd);
		}
	    }
	}
	else {
	    for(auto nd: iter_post_const(*tree)) {
		if (not nd->isTip() and nd != root) {
		    const auto descendants = remap(nd->get_data().desIds);

		    add_split_if_consistent(nd, RSplit{descendants, leafTaxaIndices});
		}
	    }
	}
    }

    // 2. Construct final tree and add names
    auto tree = BUILD(all_leaves_indices, consistent);
    for(auto nd: iter_pre(*tree)) {
        if (nd->isTip()) {
            int index = nd->getOttId();
            nd->setOttId(ids[index]);
        }
    }
    add_root_and_tip_names(*tree, *taxonomy);
    add_names(*tree, taxa);
    return tree;
}

/// Create an unresolved taxonomy out of all the input trees.
unique_ptr<Tree_t> make_unresolved_tree(const vector<unique_ptr<Tree_t>>& trees, bool use_ids) {
    std::unique_ptr<Tree_t> retTree(new Tree_t());
    retTree->create_root();
    if (use_ids) {
        map<long,string> names;
        for(const auto& tree: trees) {
            for(auto nd: iter_pre_const(*tree)) {
                if (nd->isTip()) {
                    long id = nd->getOttId();
                    auto it = names.find(id);
                    if (it == names.end()) {
                        names[id] = nd->get_name();
                    }
                }
            }
        }
        for(const auto& n: names) {
            auto node = retTree->create_child(retTree->getRoot());
            node->setOttId(n.first);
            node->setName(n.second);
        }
        clearAndfillDesIdSets(*retTree);
    } else {
        set<string> names;
        for(const auto& tree: trees) {
            for(auto nd: iter_pre_const(*tree)) {
                if (nd->isTip()) {
                    names.insert(nd->get_name());
                }
            }
        }
        for(const auto& n: names) {
            auto node = retTree->create_child(retTree->getRoot());
            node->setName(n);
        }
    }
    return retTree;
}

int main(int argc, char *argv[])
{
    try
    {
	// 1. Parse command line arguments
	variables_map args = parse_cmd_line(argc,argv);
  
	ParsingRules rules;
	rules.setOttIds = not (bool)args.count("allow-no-ids");
	rules.pruneUnrecognizedInputTips = (bool)args.count("prune-unrecognized");

	bool synthesize_taxonomy = (bool)args.count("synthesize-taxonomy");
	bool cladeTips = not (bool)args.count("no-higher-tips");
	bool verbose = (bool)args.count("verbose");
	bool writeStandardized = (bool)args.count("standardize");
	if (writeStandardized) {
	    rules.setOttIds = false;
	}

	bool setRootName = (bool)args.count("root-name");
	
	vector<string> filenames = args["subproblem"].as<vector<string>>();
	
	// 2. Load trees from subproblem file(s)
	if (filenames.empty()) {
	    throw OTCError("No subproblem provided!");
	}

	vector<unique_ptr<Tree_t>> trees = get_trees<Tree_t>(filenames, rules);

	if (trees.empty()) {
	    throw OTCError("No trees loaded!");
	}

	//2.5 Load Incertae Sedis info
	std::set<long> incertae_sedis;
	if (args.count("incertae-sedis"))
	{
	    auto filename = args["incertae-sedis"].as<string>();
	    std::ifstream file(filename);
	    if (not file)
		throw OTCError()<<"Cannot open incertae sedis file '"<<filename<<"'";
	    while (file)
	    {
		long i;
		file >> i;
		incertae_sedis.insert(i);
	    }
	}

	// 3. Make a fake taxonomy if asked
	if (synthesize_taxonomy) {
	    trees.push_back(make_unresolved_tree(trees, rules.setOttIds));
	    LOG(DEBUG)<<"taxonomy = "<<newick(*trees.back())<<"\n";
	}

	// 4. Add fake Ott Ids to tips and compute desIds (if asked)
	if (not rules.setOttIds) {
	    auto name_to_id = createIdsFromNames(*trees.back());
	    for(auto& tree: trees) {
		setIdsFromNamesAndRefresh(*tree, name_to_id);
	    }
	}

	// 5. Write out subproblem with newly minted ottids (if asked)
	if (writeStandardized) {
	    for(const auto& tree: trees) {
		relabelNodesWithOttId(*tree);
		std::cout<<newick(*tree)<<"\n";
	    }
	    return 0;
	}

	// 6. Check if trees are mapping to non-terminal taxa, and either fix the situation or die.
	for (int i = 0; i <trees.size() - 1; i++) {
	    if (cladeTips) {
		expandOTTInternalsWhichAreLeaves(*trees[i], *trees.back());
	    } else {
		requireTipsToBeMappedToTerminalTaxa(*trees[i], *trees.back());
	    }
	}

        // 7. Perform the synthesis
	computeDepth(*trees.back());
	auto tree = combine(trees, incertae_sedis, verbose);

	// 8. Set the root name (if asked)
	// FIXME: This could be avoided if the taxonomy tree in the subproblem always had a name for the root node.
	if (setRootName) {
	    tree->getRoot()->setName(args["root-name"].as<string>());
	}

	// 9. Write out the summary tree.
	writeTreeAsNewick(std::cout, *tree);
	std::cout<<"\n";

	return 0;
    }
    catch (std::exception& e)
    {
	std::cerr<<"otc-solve-subproblem: Error! "<<e.what()<<std::endl;
        exit(1);
    }
}
