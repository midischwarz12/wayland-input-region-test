#define main client_main
#include "../main.c"
#undef main
#include <assert.h>

int main(void)
{
    size_t size;
    assert(buffer_size(560, 360, &size) && size == 560 * 360 * 4);
    assert(!buffer_size(0, 360, &size));
    assert(!buffer_size(560, -1, &size));
    assert(!buffer_size(INT_MAX, 1, &size));
    assert(!buffer_size(16384, 16384, &size));

    // Small compositor-configured sizes must clip the painted region without overruns.
    const int sizes[][2] = {{560, 360}, {240, 160}, {30, 30}, {1, 1}};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        int width = sizes[i][0], height = sizes[i][1];
        assert(buffer_size(width, height, &size));
        uint32_t *pixels = malloc(size + sizeof(uint32_t));
        assert(pixels);
        pixels[size / 4] = 0x12345678;
        for (int mode = INPUT_RECT; mode <= INPUT_FULL; mode++) {
            paint(pixels, width, height, mode);
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int visible = mode != INPUT_EMPTY && x >= INPUT_X && y >= INPUT_Y &&
                                  x < INPUT_X + INPUT_WIDTH && y < INPUT_Y + INPUT_HEIGHT;
                    assert(pixels[(size_t)y * width + x] == (visible ? 0xff20c878 : 0));
                }
            }
            assert(pixels[size / 4] == 0x12345678);
        }
        free(pixels);
    }
    puts("buffer sizing and transparency tests passed");
    return 0;
}
