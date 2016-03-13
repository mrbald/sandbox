#include "logger.h"

#include <boost/circular_buffer.hpp>
#include <boost/intrusive/set.hpp>

namespace ufw {

namespace ive = boost::intrusive;

template <class T, class Cmp = std::less<>>
struct sorted_circular_buffer
{
    sorted_circular_buffer(size_t cap): ring(cap) {}

    template <class X>
    void put(X&& x)
    {
        if (ring.full())
            ring.set_capacity(ring.capacity() * 2);

        // TODO: VL: key extractor, etc.
        typename tree_t::insert_commit_data commit_data;
        auto res = tree.insert_check(x, node_cmp {}, commit_data);
        if (res.second)
        {
            ring.push_back(node{std::forward<X>(x)});
            tree.insert_commit(ring.back(), commit_data);
        }
        else
        {
            res.first->data = std::forward<X>(x); // TODO: VL: merging policy
        }
    }

    bool take(T& t)
    {
        if (ring.empty())
            return false;
        t = std::move(ring.front().data);
        ring.pop_front();
        return true;
    }

private:
    struct node: ive::set_base_hook<ive::link_mode<ive::auto_unlink>, ive::optimize_size<false>>
    {
        T data;
        template <class... Args>
        node(Args&&... args): data(std::forward<Args>(args)...) {}
    };

    struct node_cmp
    {
        Cmp cmp;
        bool operator()(node const& l, node const& r) const { return cmp(l.data, r.data); }
        bool operator()(T const& l, node const& r) const { return cmp(l, r.data); }
        bool operator()(node const& l, T const& r) const { return cmp(l.data, r); }
    };

    using ring_t = boost::circular_buffer<node>;
    ring_t ring;

    using tree_t = ive::set<node, ive::compare<node_cmp>, ive::constant_time_size<false>>;
    tree_t tree;
};

} // namespace ufw

int main()
{
    ufw::sorted_circular_buffer<uint64_t> buf(1024);

    buf.put(42ul);
    buf.put(42ul);
    uint64_t x;
    if (buf.take(x))
        LOG_INF << "popped: " << x;
    if (buf.take(x))
        LOG_INF << "popped: " << x;
}
