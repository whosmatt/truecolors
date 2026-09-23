#include "musicgate.h"
#include "unity.h"

static musicgate_t g;

static bool run(float p, int blocks)
{
    bool open = false;
    for (int i = 0; i < blocks; i++) {
        open = musicgate_block(&g, p);
    }
    return open;
}

TEST_CASE("closed until the window is mostly music", "[musicgate]")
{
    musicgate_init(&g);
    TEST_ASSERT_FALSE(musicgate_block(&g, 0.0f));
    // Per-block value, measured per take: one block must not open it.
    TEST_ASSERT_FALSE(musicgate_block(&g, 1.0f));
}

TEST_CASE("opens on sustained music", "[musicgate]")
{
    musicgate_init(&g);
    TEST_ASSERT_TRUE(run(0.95f, MUSICGATE_WIN));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, musicgate_fraction(&g));
}

TEST_CASE("stays closed on non-music", "[musicgate]")
{
    musicgate_init(&g);
    // 0.198 is the measured median on non-music.
    TEST_ASSERT_FALSE(run(0.198f, MUSICGATE_WIN * 2));
}

TEST_CASE("holds through a gap, then closes", "[musicgate]")
{
    musicgate_init(&g);
    TEST_ASSERT_TRUE(run(0.95f, MUSICGATE_WIN));
    // Median falls after half a window, then the hold-off runs.
    TEST_ASSERT_TRUE(run(0.0f, MUSICGATE_WIN / 2));
    TEST_ASSERT_TRUE(run(0.0f, MUSICGATE_HOLDOFF / 2));
    TEST_ASSERT_FALSE(run(0.0f, MUSICGATE_WIN + MUSICGATE_HOLDOFF));
}

TEST_CASE("a brief dropout does not close the gate", "[musicgate]")
{
    musicgate_init(&g);
    TEST_ASSERT_TRUE(run(0.95f, MUSICGATE_WIN));
    TEST_ASSERT_TRUE(run(0.0f, 20));        // one quiet bar
    TEST_ASSERT_TRUE(run(0.95f, 20));
}

TEST_CASE("threshold is applied per block, not averaged", "[musicgate]")
{
    // Below threshold never counts, however many.
    musicgate_init(&g);
    TEST_ASSERT_FALSE(run(0.69f, MUSICGATE_WIN * 2));
    musicgate_init(&g);
    TEST_ASSERT_TRUE(run(0.71f, MUSICGATE_WIN));
}
