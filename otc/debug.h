#ifndef OTCETERA_DEBUG_H
#define OTCETERA_DEBUG_H

#include "otc/otc_base_includes.h"
#include "otc/tree_data.h"
#include "otc/tree_operations.h"

namespace otc {

template<typename T>
bool check_node_pointers(const T & nd) {
    bool good = true;
    auto c = nd.getFirstChild();
    while (c != nullptr) {
        if (c->getParent() != &nd) {
            good = false;
            assert(c->getParent() == &nd);
        }
        c = c->getNextSib();
    }
    auto ps = nd.getPrevSib();
    assert(ps == nullptr || ps->getNextSib() == &nd);
    return good;
}

template<typename T>
bool check_all_node_pointers(const T & tree) {
    auto ns = tree.getAllAttachedNodes();
    for (auto nd : ns) {
        if (!check_node_pointers(*nd)) {
            return false;
        }
    }
    if (tree.get_root()->getParent() != nullptr) {
        assert(tree.get_root()->getParent() == nullptr);
        return false;
    }
    return true;
}

template<typename T>
bool check_all_node_pointers_iter(const T & node) {
   for (auto nd : iter_pre_n_const(&node)) {
        if (!check_node_pointers(*nd)) {
            return false;
        }
    }
    return true;
}

template<typename T>
bool check_preorder(const T & tree) {
    auto ns = tree.getSetOfAllAttachedNodes();
    std::set<const typename T::node_type *> visited;
    for (auto nd : iter_pre_const(tree)) {
        auto p = nd->getParent();
        if (p) {
            if (!contains(visited, p)) {
                assert(contains(visited, p));
                return false;
            }
        }
        visited.insert(nd);
    }
    if (visited != ns) {
        assert(visited == ns);
        return false;
    }
    return true;
}

template<typename T>
bool check_postorder(const T & tree) {
    auto ns = tree.getSetOfAllAttachedNodes();
    std::set<const typename T::node_type *> visited;
    for (auto nd : iter_post_const(tree)) {
        auto p = nd->getParent();
        if (p) {
            if (contains(visited, p)) {
                assert(!contains(visited, p));
                return false;
            }
        }
        visited.insert(nd);
    }
    if (visited != ns) {
        assert(visited == ns);
        return false;
    }
    return true;
}

template<typename T>
bool check_child_iter(const T & tree) {
    auto ns = tree.getAllAttachedNodes();
    for (auto nd : ns) {
        auto nc = nd->getOutDegree();
        auto v = 0U;
        for (auto c : iter_child_const(*nd)) {
            assert(c->getParent() == nd);
            ++v;
        }
        assert(v == nc);
    }
    return true;
}

template<typename T>
bool check_des_ids(const T & tree);

template<typename T>
inline bool check_des_ids(const T & ) {
    return true;
}

template<>
inline bool check_des_ids(const TreeMappedWithSplits & tree) {
    auto ns = tree.getSetOfAllAttachedNodes();
    for (auto nd : ns) {
        if (nd->isTip()) {
            continue;
        }
        std::set<long> d;
        auto sum = 0U;
        for (auto c : iter_child_const(*nd)) {
            const auto & cd = c->get_data().desIds;
            sum += cd.size();
            assert(cd.size() > 0);
            d.insert(cd.begin(), cd.end());
        }
        assert(sum == d.size());
        if (tree.get_data().desIdSetsContainInternals) {
            if (nd->has_ott_id()) {
                d.insert(nd->get_ott_id());
            }
        }
        if (!isSubset(d, nd->get_data().desIds)) {
            std::cerr << "node " << nd->get_ott_id() << '\n';
            writeOttSetDiff(std::cerr, " ", nd->get_data().desIds, " node.desId ", d, " calc.");
            assert(isSubset(d, nd->get_data().desIds));
        }
    }
    return true;
}


template<typename T>
bool check_tree_invariants(const T & tree) {
    if (!check_all_node_pointers(tree)) {
        return false;
    }
    if (!check_preorder(tree)) {
        return false;
    }
    if (!check_postorder(tree)) {
        return false;
    }
    if (!check_child_iter(tree)) {
        return false;
    }
    if (!check_des_ids(tree)) {
        return false;
    }
    return true;
}

} // namespace otc
#endif

