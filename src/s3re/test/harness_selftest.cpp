// Tests of the test harness itself: if these fail, no other result can be
// trusted.

#include "test/difftest.h"

namespace s3re::test {

S3RE_TEST(Harness_RngIsDeterministic) {
    Rng a(12345);
    Rng b(12345);
    for (int i = 0; i < 1000; ++i) {
        S3RE_EXPECT_EQ(a.Next64(), b.Next64());
    }
    // Known first output of SplitMix64 for seed 0.
    Rng zero(0);
    S3RE_EXPECT_EQ(zero.Next64(), 0xe220a8397b1dcdafull);
}

S3RE_TEST(Harness_RngRangesStayInBounds) {
    for (int i = 0; i < 10000; ++i) {
        const std::uint32_t below = ctx.rng().Below(7);
        S3RE_EXPECT(below < 7);
        const std::int32_t range = ctx.rng().Range(-3, 3);
        S3RE_EXPECT(range >= -3 && range <= 3);
    }
}

S3RE_TEST(Harness_DetectsDifferences) {
    Context probe("probe", 1, /*quiet=*/true);
    probe.ExpectEq(1, 2, "1", "2", __FILE__, __LINE__);
    S3RE_EXPECT_EQ(probe.failures(), 1);

    const unsigned char x[4] = {1, 2, 3, 4};
    const unsigned char y[4] = {1, 2, 3, 5};
    probe.ExpectMemEq(x, y, sizeof(x), "x", "y", __FILE__, __LINE__);
    S3RE_EXPECT_EQ(probe.failures(), 2);

    probe.ExpectMemEq(x, x, sizeof(x), "x", "x", __FILE__, __LINE__);
    probe.ExpectEq(7, 7, "7", "7", __FILE__, __LINE__);
    S3RE_EXPECT_EQ(probe.failures(), 2);
}

}  // namespace s3re::test
