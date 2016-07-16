#include "logger.h"

#include <boost/circular_buffer.hpp>
#include <boost/intrusive/set.hpp>

/*
   Copyright 2016 Vladimir Lysyy (mrbald@github)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

namespace ufw {

namespace ive = boost::intrusive;

enum class insert_mode: uint8_t
{
    OVERWRITE = 0, // new element overwrites the existing one
    MERGE,         //
    PUSH,          // new element disables conflation of the existing one,
                      but itself becomes eligible for future conflation
    NO_CONFLATION  // conflation disabled
};

enum class position_mode: uint8_t
{
    RETAIN = 0, // merge result inserted in place of the existing one
    GIVEUP      // merge result enqueued to the back (not optimal for queues with contiguous store)
};

template <class T, class Cmp = std::less<>>
struct sorted_circular_buffer
{
    sorted_circular_buffer(size_t cap): ring(cap) {}

    template <class X>
    bool put(X&& x)
    {
        if (ring.full())
            ring.set_capacity(ring.capacity() * 2);

        // TODO: VL: key extractor, etc.
        typename tree_t::insert_commit_data commit_data;
        auto res = tree.insert_check(x, CmpAdapter {}, commit_data);
        if (res.second)
        {
            ring.push_back(node{std::forward<X>(x)});
            tree.insert_commit(ring.back(), commit_data);
            return true;
        }
        else
        {
            res.first->data = std::forward<X>(x); // TODO: VL: merging policy
            return false;
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

    struct CmpAdapter
    {
        Cmp cmp;
        bool operator()(node const& l, node const& r) const { return cmp(l.data, r.data); }
        bool operator()(T const& l, node const& r) const { return cmp(l, r.data); }
        bool operator()(node const& l, T const& r) const { return cmp(l.data, r); }
    };

    using ring_t = boost::circular_buffer<node>;
    ring_t ring;

    using tree_t = ive::set<node, ive::compare<CmpAdapter>, ive::constant_time_size<false>>;
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
