#include <unity.h>

#include "screensavers/LifeGrid.h"

void setUp() {}
void tearDown() {}

void test_set_and_read_cell_roundtrips() {
    standby::PackedGridStorage grid{};
    TEST_ASSERT_FALSE(standby::cellAlive(grid, 35));
    standby::setCell(grid, 35, true);
    TEST_ASSERT_TRUE(standby::cellAlive(grid, 35));
    standby::setCell(grid, 35, false);
    TEST_ASSERT_FALSE(standby::cellAlive(grid, 35));
}

void test_set_cell_at_clips_out_of_bounds() {
    standby::PackedGridStorage grid{};
    standby::setCellAt(grid, 8, 8, -1, 4, true); // off-grid, no-op
    standby::setCellAt(grid, 8, 8, 8, 4, true); // off-grid, no-op
    TEST_ASSERT_FALSE(standby::anyCellAlive(standby::viewOf(grid, standby::packedWordCount(64))));
    standby::setCellAt(grid, 8, 8, 3, 2, true);
    TEST_ASSERT_TRUE(standby::cellAlive(grid, 2 * 8 + 3));
}

void test_blinker_oscillates_period_two() {
    // A vertical 3-cell blinker becomes horizontal after one step, then vertical
    // again after the next -- the canonical Game-of-Life period-2 oscillator.
    constexpr uint16_t cols = 8;
    constexpr uint16_t rows = 8;
    standby::IncrementalLifeGrid grid;
    grid.reset(cols, rows);
    grid.setAliveAt(4, 3, true);
    grid.setAliveAt(4, 4, true);
    grid.setAliveAt(4, 5, true);
    grid.finishSeed();

    TEST_ASSERT_EQUAL_UINT32(3, grid.step());
    // Now horizontal: (3,4) (4,4) (5,4).
    TEST_ASSERT_TRUE(standby::cellAlive(grid.liveCells(), 4 * cols + 3));
    TEST_ASSERT_TRUE(standby::cellAlive(grid.liveCells(), 4 * cols + 4));
    TEST_ASSERT_TRUE(standby::cellAlive(grid.liveCells(), 4 * cols + 5));
    TEST_ASSERT_FALSE(standby::cellAlive(grid.liveCells(), 3 * cols + 4));
    TEST_ASSERT_FALSE(standby::cellAlive(grid.liveCells(), 5 * cols + 4));
    // Only the four cells that flipped need repainting.
    TEST_ASSERT_TRUE(standby::cellAlive(grid.dirtyCells(), 3 * cols + 4));
    TEST_ASSERT_FALSE(standby::cellAlive(grid.dirtyCells(), 4 * cols + 4));

    TEST_ASSERT_EQUAL_UINT32(3, grid.step());
    // Back to the original vertical blinker.
    TEST_ASSERT_TRUE(standby::cellAlive(grid.liveCells(), 3 * cols + 4));
    TEST_ASSERT_TRUE(standby::cellAlive(grid.liveCells(), 4 * cols + 4));
    TEST_ASSERT_TRUE(standby::cellAlive(grid.liveCells(), 5 * cols + 4));
}

void test_empty_grid_stays_empty() {
    standby::IncrementalLifeGrid grid;
    grid.reset(8, 8);
    grid.finishSeed();
    TEST_ASSERT_EQUAL_UINT32(0, grid.step());
    TEST_ASSERT_FALSE(standby::anyCellAlive(grid.liveCells()));
}

void test_advance_rng_is_deterministic() {
    uint32_t a = 12345;
    uint32_t b = 12345;
    TEST_ASSERT_EQUAL_UINT32(standby::advanceRng(a), standby::advanceRng(b));
    TEST_ASSERT_EQUAL_UINT32(a, b);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_and_read_cell_roundtrips);
    RUN_TEST(test_set_cell_at_clips_out_of_bounds);
    RUN_TEST(test_blinker_oscillates_period_two);
    RUN_TEST(test_empty_grid_stays_empty);
    RUN_TEST(test_advance_rng_is_deterministic);
    return UNITY_END();
}
