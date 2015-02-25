#include <set>
#include "otc/otcli.h"
#include "otc/tree_operations.h"
#include "otc/tree_data.h"
using namespace otc;

typedef otc::RootedTreeNode<RTSplits> Node_t;
typedef otc::RootedTree<typename Node_t::data_type, RTreeOttIDMapping<typename Node_t::data_type>> Tree_t;
bool processNextTree(OTCLI & otCLI, std::unique_ptr<Tree_t> tree);
bool handleTabooOTTIdListFile(OTCLI & otCLI, const std::string &nextArg);
struct RemapToDeepestUnlistedState {
	std::unique_ptr<Tree_t> taxonomy;
	int numErrors;
	std::set<long> ottIds;
	std::set<const Node_t *> contestedNodes;
	std::set<long> tabooIds;

	RemapToDeepestUnlistedState()
		:taxonomy(nullptr),
		 numErrors(0) {
		}

	void summarize(const OTCLI &otCLI) {
		assert (taxonomy != nullptr);
		for (auto nd : contestedNodes) {
			otCLI.out << nd->getName() << '\n';
		}
	}

	bool processTaxonomyTree(OTCLI & otCLI) {
		ottIds = keys(taxonomy->getData().ottIdToNode);
		otCLI.getParsingRules().ottIdValidator = &ottIds;
		return true;
	}

	bool processSourceTree(OTCLI & otCLI, std::unique_ptr<Tree_t> tree) {
		assert(taxonomy != nullptr);
		assert(tree != nullptr);
		expandOTTInternalsWhichAreLeaves(*tree, *taxonomy);
		return processExpandedTree(otCLI, *tree);
	}

	bool processExpandedTree(OTCLI &, Tree_t & tree) {
		std::map<const Node_t *, std::set<long> > prunedDesId;
		for (auto nd : ConstLeafIter<Tree_t>(tree)) {
			auto ottId = nd->getOttId();
			markPathToRoot(*taxonomy, ottId, prunedDesId);
		}
		std::map<std::set<long>, std::list<const Node_t *> > taxCladesToTaxNdList;
		for (auto & nodeSetPair : prunedDesId) {
			auto nd = nodeSetPair.first;
			auto & ds = nodeSetPair.second;
			if (ds.size() < 2) {
				continue;
			}
			auto tctnlIt = taxCladesToTaxNdList.find(ds);
			if (tctnlIt == taxCladesToTaxNdList.end()) {
				std::list<const Node_t *> sel{1, nd};
				taxCladesToTaxNdList.emplace(ds, sel);
			} else {
				tctnlIt->second.push_back(nd);
			}
		}
		std::set<std::set<long> > sourceClades;
		for (auto nd : PostorderInternalIter<Tree_t>(tree)) {
			if (nd->getParent() != nullptr && !nd->isTip()) {
				sourceClades.insert(std::move(nd->getData().desIds));
			}
		}
		auto numLeaves = tree.getRoot()->getData().desIds.size();
		recordContested(taxCladesToTaxNdList, sourceClades, contestedNodes, numLeaves);
		return true;
	}

	void recordContested(const std::map<std::set<long>, std::list<const Node_t *> > & prunedDesId,
						 const std::set<std::set<long> > & sourceClades,
						 std::set<const Node_t *> & contestedSet,
						 std::size_t numLeaves) {
		for (const auto & pd : prunedDesId) {
			// shortcircuite taxon nodes that are already marked as contested
			const auto & ndlist = pd.second;
			bool allContested = true;
			for (auto nd : ndlist) {
				if (!contains(contestedSet, nd)) {
					allContested = false;
					break;
				}
			}
			if (allContested) {
				continue;
			}
			const auto & taxNodesDesSets = pd.first;
			const auto nss = taxNodesDesSets.size();
			if (nss == 1 || nss == numLeaves) {
				continue;
			}
			for (const auto & sc : sourceClades) {
				if (!areCompatibleDesIdSets(taxNodesDesSets, sc)) {
					for (auto nd : ndlist) {
						contestedSet.insert(nd);
					}
					break;
				}
			}
		}
	}

	bool parseAndTabooOTTIdListFile(const std::string &fp) {
		auto t = parseListOfOttIds(fp);
		tabooIds.insert(begin(t), end(t));
		return true;
	}
};

inline bool processNextTree(OTCLI & otCLI, std::unique_ptr<Tree_t> tree) {
	RemapToDeepestUnlistedState * ctsp = static_cast<RemapToDeepestUnlistedState *>(otCLI.blob);
	assert(ctsp != nullptr);
	assert(tree != nullptr);
	if (ctsp->taxonomy == nullptr) {
		ctsp->taxonomy = std::move(tree);
		return ctsp->processTaxonomyTree(otCLI);
	}
	return ctsp->processSourceTree(otCLI, std::move(tree));
}

bool handleTabooOTTIdListFile(OTCLI & otCLI, const std::string &nextArg) {
	RemapToDeepestUnlistedState * fusp = static_cast<RemapToDeepestUnlistedState *>(otCLI.blob);
	assert(fusp != nullptr);
	assert(!nextArg.empty());
	return fusp->parseAndTabooOTTIdListFile(nextArg);
}

int main(int argc, char *argv[]) {
	OTCLI otCLI("otcdetectcontested",
				"takes at least 2 newick file paths: a full taxonomy tree, and some number of input trees. Writes the OTT IDs of clades in the taxonomy whose monophyly is questioned by at least one input",
				"taxonomy.tre inp1.tre inp2.tre");
	RemapToDeepestUnlistedState fus;
	otCLI.blob = static_cast<void *>(&fus);
	otCLI.addFlag('m',
				  "ARG=a file containing a list of taboo OTT ids.",
				  handleTabooOTTIdListFile,
				  true);
	auto rc = treeProcessingMain<Tree_t>(otCLI, argc, argv, processNextTree, nullptr, 2);
	if (rc == 0) {
		fus.summarize(otCLI);
		return fus.numErrors;
	}
	return rc;
}
