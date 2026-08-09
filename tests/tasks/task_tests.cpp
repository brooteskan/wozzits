// tests/tasks/task_tests.cpp
//
// S0 coverage for the wz::tasks fork-join API on the serial-inline backend. These
// pin the SEMANTICS the later pool/fibre backends must preserve, not the serial
// implementation: each run() task runs exactly once, an index fan-out covers
// [0, n), and -- the property the cognition collect sweep depends on -- a task may
// itself run()/wait() nested and the join sees every child's result.

#include <gtest/gtest.h>

#include <tasks/task.h>

#include <cstddef>
#include <vector>

namespace
{
    using wz::tasks::Counter;
    using wz::tasks::run;
    using wz::tasks::wait;

    TEST(TasksSerialBackend, RunsSingleTaskOnce)
    {
        int ran = 0;
        Counter c;
        run(c, [](void* u) { ++*static_cast<int*>(u); }, &ran);
        wait(c);
        EXPECT_EQ(ran, 1);
    }

    TEST(TasksSerialBackend, IndexFanOutCoversRangeInOrder)
    {
        std::vector<std::size_t> seen;
        Counter c;
        run(c, 5, [](std::size_t i, void* u)
            { static_cast<std::vector<std::size_t>*>(u)->push_back(i); }, &seen);
        wait(c);

        ASSERT_EQ(seen.size(), 5u);
        for (std::size_t i = 0; i < seen.size(); ++i)
            EXPECT_EQ(seen[i], i);  // serial backend runs 0..n-1 in order
    }

    TEST(TasksSerialBackend, ZeroIndexTasksIsANoOp)
    {
        int ran = 0;
        Counter c;
        run(c, 0, [](std::size_t, void* u) { ++*static_cast<int*>(u); }, &ran);
        wait(c);
        EXPECT_EQ(ran, 0);
    }

    // The fork-join property the cognition collect sweep is built on: a task
    // recurses into its children via run(), wait()s, then combines their results.
    // This mirrors bp_collect_subtree exactly -- fan out child subtrees, join,
    // fold in fixed child order. Serially it is DFS; the result must be correct.
    struct Node
    {
        int value;
        std::vector<Node> children;
    };

    struct SumFrame
    {
        const Node* node;
        long long result;
    };

    void sum_subtree(void* user)
    {
        auto& frame = *static_cast<SumFrame*>(user);
        const Node& n = *frame.node;

        std::vector<SumFrame> kids;
        kids.reserve(n.children.size());
        for (const Node& child : n.children)
            kids.push_back(SumFrame{ &child, 0 });

        Counter c;
        run(c, kids.size(), [](std::size_t i, void* u)
            { sum_subtree(&static_cast<SumFrame*>(u)[i]); }, kids.data());
        wait(c);  // nested join -- fibre suspend point at S2

        long long total = n.value;
        for (const SumFrame& k : kids)  // fixed child-order combine
            total += k.result;
        frame.result = total;
    }

    TEST(TasksSerialBackend, NestedRunWaitContractsATree)
    {
        // 1 -> { 2 -> { 4, 5 }, 3 }
        Node root{ 1, {} };
        root.children.push_back(Node{ 2, {} });
        root.children.push_back(Node{ 3, {} });
        root.children[0].children.push_back(Node{ 4, {} });
        root.children[0].children.push_back(Node{ 5, {} });

        SumFrame frame{ &root, 0 };
        Counter c;
        run(c, sum_subtree, &frame);
        wait(c);

        EXPECT_EQ(frame.result, 1 + 2 + 3 + 4 + 5);
    }
}
