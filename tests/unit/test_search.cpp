#include <gtest/gtest.h>
#include "bcss/search.hpp"

using namespace bcss;

// Section 10: Adversarial Candidate Ranking Verification
// Key: (k, max_disp, total_disp)

TEST(SearchTest, TestA_K_Dominates) {
    PathCandidate A; A.k = 1; A.max_disp = 10; A.total_disp = 10;
    PathCandidate B; B.k = 2; B.max_disp = 1;  B.total_disp = 2;

    // k=1 dominates k=2
    EXPECT_TRUE(A < B);
    EXPECT_FALSE(B < A);
}

TEST(SearchTest, TestB_DeltaMax_Dominates_After_Equal_K) {
    PathCandidate A; A.k = 2; A.max_disp = 3; A.total_disp = 6;
    PathCandidate B; B.k = 2; B.max_disp = 4; A.total_disp = 4;

    // max_disp=3 beats max_disp=4 when k is equal
    EXPECT_TRUE(A < B);
    EXPECT_FALSE(B < A);
}

TEST(SearchTest, TestC_DeltaTotal_Final_Criterion) {
    PathCandidate A; A.k = 2; A.max_disp = 3; A.total_disp = 5;
    PathCandidate B; B.k = 2; B.max_disp = 3; B.total_disp = 7;

    // total_disp=5 beats total_disp=7 when k and max_disp are equal
    EXPECT_TRUE(A < B);
    EXPECT_FALSE(B < A);
}
