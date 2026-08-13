#include <gtest/gtest.h>
#include "bcss/dependencies.hpp"

using namespace bcss;

TEST(DependenciesTest, SelfDependencyAndCycle) {
    DependencyGraph graph;
    std::string err;

    EXPECT_FALSE(graph.add_dependency(1, 1)); // Self-dependency invalid

    EXPECT_TRUE(graph.add_dependency(1, 2));
    EXPECT_TRUE(graph.add_dependency(2, 3));
    EXPECT_FALSE(graph.has_cycle());

    EXPECT_TRUE(graph.add_dependency(3, 1)); // Creates cycle 1 -> 2 -> 3 -> 1
    EXPECT_TRUE(graph.has_cycle());
    EXPECT_FALSE(graph.validate_dag(err));
}
