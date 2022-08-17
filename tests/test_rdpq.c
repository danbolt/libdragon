#include <malloc.h>
#include <rspq.h>
#include <rspq_constants.h>
#include <rdpq.h>
#include <rdp_commands.h>
#include "../src/rspq/rspq_internal.h"
#include "../src/rdpq/rdpq_internal.h"
#include "../src/rdpq/rdpq_debug.h"
#include "../src/rdpq/rdpq_constants.h"

#define RDPQ_INIT() \
    rspq_init(); DEFER(rspq_close()); \
    rdpq_init(); DEFER(rdpq_close()); \
    rdpq_debug_start(); DEFER(rdpq_debug_stop())

static void surface_clear(surface_t *s, uint8_t c) {
    memset(s->buffer, c, s->height * s->stride);
}

__attribute__((unused))
static void debug_surface(const char *name, uint16_t *buf, int w, int h) {
    debugf("Surface %s:\n", name);
    for (int j=0;j<h;j++) {
        for (int i=0;i<w;i++) {
            debugf("%04x ", buf[j*w+i]);
        }
        debugf("\n");
    }
    debugf("\n");
}

__attribute__((unused))
static void debug_surface32(const char *name, uint32_t *buf, int w, int h) {
    debugf("Surface %s:\n", name);
    for (int j=0;j<h;j++) {
        for (int i=0;i<w;i++) {
            debugf("%08lx ", buf[j*w+i]);
        }
        debugf("\n");
    }
    debugf("\n");
}

void test_rdpq_rspqwait(TestContext *ctx)
{
    // Verify that rspq_wait() correctly also wait for RDP to terminate
    // all its scheduled operations.
    surface_t fb = surface_alloc(FMT_RGBA32, 128, 128);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);
    uint32_t *framebuffer = fb.buffer;

    RDPQ_INIT();

    color_t color = RGBA32(0x11, 0x22, 0x33, 0xFF);

    rdpq_set_mode_fill(color);
    rdpq_set_color_image(&fb);
    rdpq_fill_rectangle(0, 0, 128, 128);
    rspq_wait();

    // Sample the end of the buffer immediately after rspq_wait. If rspq_wait
    // doesn't wait for RDP to become idle, this pixel will not be filled at
    // this point. 
    ASSERT_EQUAL_HEX(framebuffer[127*128+127], color_to_packed32(color),
        "invalid color in framebuffer at (127,127)");
}

void test_rdpq_clear(TestContext *ctx)
{
    RDPQ_INIT();

    color_t fill_color = RGBA32(0xFF, 0xFF, 0xFF, 0xFF);

    surface_t fb = surface_alloc(FMT_RGBA16, 32, 32);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    rdpq_set_mode_fill(fill_color);
    rdpq_set_color_image(&fb);
    rdpq_fill_rectangle(0, 0, 32, 32);
    rspq_wait();

    uint16_t *framebuffer = fb.buffer;
    for (uint32_t i = 0; i < 32 * 32; i++)
    {
        ASSERT_EQUAL_HEX(framebuffer[i], color_to_packed16(fill_color),
            "Framebuffer was not cleared properly! Index: %lu", i);
    }
}

void test_rdpq_dynamic(TestContext *ctx)
{
    RDPQ_INIT();

    const int WIDTH = 64;
    surface_t fb = surface_alloc(FMT_RGBA16, WIDTH, WIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    uint16_t expected_fb[WIDTH*WIDTH];
    memset(expected_fb, 0, sizeof(expected_fb));

    rdpq_set_mode_fill(RGBA32(0,0,0,0));
    rdpq_set_color_image(&fb);

    for (uint32_t y = 0; y < WIDTH; y++)
    {
        for (uint32_t x = 0; x < WIDTH; x += 4)
        {
            color_t c = RGBA16(x, y, x+y, x^y);
            expected_fb[y * WIDTH + x] = color_to_packed16(c);
            expected_fb[y * WIDTH + x + 1] = color_to_packed16(c);
            expected_fb[y * WIDTH + x + 2] = color_to_packed16(c);
            expected_fb[y * WIDTH + x + 3] = color_to_packed16(c);
            rdpq_set_fill_color(c);
            rdpq_set_scissor(x, y, x + 4, y + 1);
            rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
        }
    }

    rspq_wait();
    
    //dump_mem(framebuffer, TEST_RDPQ_FBSIZE);
    //dump_mem(expected_fb, TEST_RDPQ_FBSIZE);

    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, "Framebuffer contains wrong data!");
}

void test_rdpq_passthrough_big(TestContext *ctx)
{
    RDPQ_INIT();

    const int WIDTH = 16;
    surface_t fb = surface_alloc(FMT_RGBA16, WIDTH, WIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    uint16_t expected_fb[WIDTH*WIDTH];
    memset(expected_fb, 0xFF, sizeof(expected_fb));

    rdpq_set_color_image(&fb);
    rdpq_set_blend_color(RGBA32(255,255,255,255));
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER1((ZERO,ZERO,ZERO,ZERO), (ZERO,ZERO,ZERO,ZERO)));
    rdpq_mode_blending(RDPQ_BLENDER((IN_RGB, ZERO, BLEND_RGB, ONE)) | SOM_BLENDING);

    rdp_draw_filled_triangle(0, 0, WIDTH, 0, WIDTH, WIDTH);
    rdp_draw_filled_triangle(0, 0, 0, WIDTH, WIDTH, WIDTH);

    rspq_wait();
    
    //dump_mem(framebuffer, TEST_RDPQ_FBSIZE);
    //dump_mem(expected_fb, TEST_RDPQ_FBSIZE);

    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, "Framebuffer contains wrong data!");
}

void test_rdpq_block(TestContext *ctx)
{
    RDPQ_INIT();

    const int WIDTH = 64;
    surface_t fb = surface_alloc(FMT_RGBA16, WIDTH, WIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    uint16_t expected_fb[WIDTH*WIDTH];
    memset(expected_fb, 0, sizeof(expected_fb));

    rspq_block_begin();
    rdpq_set_mode_fill(RGBA32(0,0,0,0));

    for (uint32_t y = 0; y < WIDTH; y++)
    {
        for (uint32_t x = 0; x < WIDTH; x += 4)
        {
            color_t c = RGBA16(x, y, x+y, x^y);
            expected_fb[y * WIDTH + x]     = color_to_packed16(c);
            expected_fb[y * WIDTH + x + 1] = color_to_packed16(c);
            expected_fb[y * WIDTH + x + 2] = color_to_packed16(c);
            expected_fb[y * WIDTH + x + 3] = color_to_packed16(c);
            rdpq_set_fill_color(c);
            rdpq_set_scissor(x, y, x + 4, y + 1);
            rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
        }
    }
    rspq_block_t *block = rspq_block_end();
    DEFER(rspq_block_free(block));

    rdpq_set_color_image(&fb);
    rspq_block_run(block);
    rspq_wait();
    
    //dump_mem(framebuffer, TEST_RDPQ_FBSIZE);
    //dump_mem(expected_fb, TEST_RDPQ_FBSIZE);

    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, "Framebuffer contains wrong data!");
}

void test_rdpq_block_coalescing(TestContext *ctx)
{
    RDPQ_INIT();

    // The actual commands don't matter because they are never executed
    rspq_block_begin();

    // These 3 commands are supposed to go to the static RDP buffer, and
    // the 3 RSPQ_CMD_RDP commands will be coalesced into one 
    rdpq_set_env_color(RGBA32(0,0,0,0));
    rdpq_set_blend_color(RGBA32(0, 0, 0, 0));
    rdpq_fill_rectangle(0, 0, 0, 0);

    // This command is a fixup
    rdpq_set_fill_color(RGBA16(0, 0, 0, 0));

    // These 3 should also have their RSPQ_CMD_RDP coalesced
    rdpq_set_env_color(RGBA32(0,0,0,0));
    rdpq_set_blend_color(RGBA32(0, 0, 0, 0));
    rdpq_fill_rectangle(0, 0, 0, 0);

    rspq_block_t *block = rspq_block_end();
    DEFER(rspq_block_free(block));

    uint64_t *rdp_cmds = (uint64_t*)block->rdp_block->cmds;

    uint32_t expected_cmds[] = {
        // auto sync + First 3 commands + auto sync
        (RSPQ_CMD_RDP_SET_BUFFER << 24) | PhysicalAddr(rdp_cmds + 5), 
            PhysicalAddr(rdp_cmds), 
            PhysicalAddr(rdp_cmds + RDPQ_BLOCK_MIN_SIZE/2),
        // Fixup command (leaves a hole in rdp block)
        (RDPQ_CMD_SET_FILL_COLOR_32 + 0xC0) << 24, 
            0,
        // Last 3 commands
        (RSPQ_CMD_RDP_APPEND_BUFFER << 24) | PhysicalAddr(rdp_cmds + 9),
    };

    ASSERT_EQUAL_MEM((uint8_t*)block->cmds, (uint8_t*)expected_cmds, sizeof(expected_cmds), "Block commands don't match!");
}

void test_rdpq_block_contiguous(TestContext *ctx)
{
    RDPQ_INIT();

    const int WIDTH = 64;
    surface_t fb = surface_alloc(FMT_RGBA16, WIDTH, WIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    uint16_t expected_fb[WIDTH*WIDTH];
    memset(expected_fb, 0xFF, sizeof(expected_fb));

    rspq_block_begin();
    /* 1: implicit sync pipe */
    /* 2: */ rdpq_set_color_image(&fb);
    /* 3: implicit set fill color */ 
    /* 4: implicit set scissor */
    /* 5: */ rdpq_set_mode_fill(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
    /* 6: implicit set scissor */
    /* 7: set fill color */
    /* 8: */ rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    /* 9: */ rdpq_fence(); // Put the fence inside the block so RDP never executes anything outside the block
    rspq_block_t *block = rspq_block_end();
    DEFER(rspq_block_free(block));

    rspq_block_run(block);
    rspq_syncpoint_wait(rspq_syncpoint_new());

    uint64_t *rdp_cmds = (uint64_t*)block->rdp_block->cmds;

    ASSERT_EQUAL_HEX(*DP_START, PhysicalAddr(rdp_cmds), "DP_START does not point to the beginning of the block!");
    ASSERT_EQUAL_HEX(*DP_END, PhysicalAddr(rdp_cmds + 9), "DP_END points to the wrong address!");

    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, "Framebuffer contains wrong data!");
}


void test_rdpq_fixup_setfillcolor(TestContext *ctx)
{
    RDPQ_INIT();

    const color_t TEST_COLOR = RGBA32(0xAA,0xBB,0xCC,0xDD);

    const int WIDTH = 64;
    surface_t fb = surface_alloc(FMT_RGBA32, WIDTH, WIDTH);
    DEFER(surface_free(&fb));

    uint32_t expected_fb32[WIDTH*WIDTH];
    memset(expected_fb32, 0, sizeof(expected_fb32));
    for (int i=0;i<WIDTH*WIDTH;i++)
        expected_fb32[i] = (TEST_COLOR.r << 24) | (TEST_COLOR.g << 16) | (TEST_COLOR.b << 8) | (TEST_COLOR.a);

    uint16_t expected_fb16[WIDTH*WIDTH];
    memset(expected_fb16, 0, sizeof(expected_fb16));
    for (int i=0;i<WIDTH*WIDTH;i++) {
        int r = TEST_COLOR.r >> 3;
        int g = TEST_COLOR.g >> 3;
        int b = TEST_COLOR.b >> 3;        
        expected_fb16[i] = ((r & 0x1F) << 11) | ((g & 0x1F) << 6) | ((b & 0x1F) << 1) | (TEST_COLOR.a >> 7);
    }

    rdpq_set_mode_fill(RGBA32(0,0,0,0));

    surface_clear(&fb, 0);
    rdpq_set_color_image(&fb);
    rdpq_set_fill_color(TEST_COLOR);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb32, WIDTH*WIDTH*4, 
        "Wrong data in framebuffer (32-bit, dynamic mode)");

    surface_clear(&fb, 0);
    rdpq_set_color_image_raw(0, PhysicalAddr(fb.buffer), FMT_RGBA16, WIDTH, WIDTH, WIDTH*2);
    rdpq_set_fill_color(TEST_COLOR);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb16, WIDTH*WIDTH*2, 
        "Wrong data in framebuffer (16-bit, dynamic mode)");

    surface_clear(&fb, 0);
    rdpq_set_fill_color(TEST_COLOR);
    rdpq_set_color_image(&fb);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb32, WIDTH*WIDTH*4, 
        "Wrong data in framebuffer (32-bit, dynamic mode, update)");

    surface_clear(&fb, 0);
    rdpq_set_fill_color(TEST_COLOR);
    rdpq_set_color_image_raw(0, PhysicalAddr(fb.buffer), FMT_RGBA16, WIDTH, WIDTH, WIDTH*2);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb16, WIDTH*WIDTH*2, 
        "Wrong data in framebuffer (16-bit, dynamic mode, update)");
}

void test_rdpq_fixup_setscissor(TestContext *ctx)
{
    RDPQ_INIT();

    const color_t TEST_COLOR = RGBA32(0xFF,0xFF,0xFF,0xFF);

    const int WIDTH = 16;
    surface_t fb = surface_alloc(FMT_RGBA16, WIDTH, WIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    uint16_t expected_fb[WIDTH*WIDTH];
    memset(expected_fb, 0, sizeof(expected_fb));
    for (int y=4;y<WIDTH-4;y++) {
        for (int x=4;x<WIDTH-4;x++) {
            expected_fb[y * WIDTH + x] = color_to_packed16(TEST_COLOR);
        }
    }

    rdpq_set_color_image(&fb);

    surface_clear(&fb, 0);
    rdpq_set_other_modes_raw(SOM_CYCLE_FILL);
    rdpq_set_fill_color(TEST_COLOR);
    rdpq_set_scissor(4, 4, WIDTH-4, WIDTH-4);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, 
        "Wrong data in framebuffer (fill mode)");

    surface_clear(&fb, 0);
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER1((ZERO,ZERO,ZERO,ZERO),(ZERO,ZERO,ZERO,ONE)));
    rdpq_mode_blending(RDPQ_BLENDER((BLEND_RGB, IN_ALPHA, IN_RGB, INV_MUX_ALPHA)));
    rdpq_set_blend_color(TEST_COLOR);
    rdpq_set_scissor(4, 4, WIDTH-4, WIDTH-4);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, 
        "Wrong data in framebuffer (1 cycle mode)");

    surface_clear(&fb, 0);
    rdpq_set_scissor(4, 4, WIDTH-4, WIDTH-4);
    rdpq_set_other_modes_raw(SOM_CYCLE_FILL);
    rdpq_set_fill_color(TEST_COLOR);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, 
        "Wrong data in framebuffer (fill mode, update)");

    surface_clear(&fb, 0);
    rdpq_set_scissor(4, 4, WIDTH-4, WIDTH-4);
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER1((ZERO,ZERO,ZERO,ZERO),(ZERO,ZERO,ZERO,ONE)));
    rdpq_mode_blending(RDPQ_BLENDER((BLEND_RGB, IN_ALPHA, IN_RGB, INV_MUX_ALPHA)));
    rdpq_set_blend_color(TEST_COLOR);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, 
        "Wrong data in framebuffer (1 cycle mode, update)");
}

void test_rdpq_fixup_texturerect(TestContext *ctx)
{
    RDPQ_INIT();

    const int FBWIDTH = 16;
    const int TEXWIDTH = FBWIDTH - 8;
    surface_t fb = surface_alloc(FMT_RGBA16, FBWIDTH, FBWIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    surface_t tex = surface_alloc(FMT_RGBA16, TEXWIDTH, TEXWIDTH);
    DEFER(surface_free(&tex));
    surface_clear(&tex, 0);

    uint16_t expected_fb[FBWIDTH*FBWIDTH];
    memset(expected_fb, 0xFF, sizeof(expected_fb));
    for (int y=0;y<TEXWIDTH;y++) {
        for (int x=0;x<TEXWIDTH;x++) {
            color_t c = RGBA16(x, y, x+y, 1);
            expected_fb[(y + 4) * FBWIDTH + (x + 4)] = color_to_packed16(c);
            ((uint16_t*)tex.buffer)[y * TEXWIDTH + x] = color_to_packed16(c);
        }
    }

    rdpq_set_color_image(&fb);
    rdpq_set_texture_image(&tex);
    rdpq_set_tile(0, FMT_RGBA16, 0, TEXWIDTH * 2, 0);
    rdpq_load_tile(0, 0, 0, TEXWIDTH, TEXWIDTH);

    surface_clear(&fb, 0xFF);
    rdpq_set_mode_copy(false);
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (copy mode, dynamic mode)");

    surface_clear(&fb, 0xFF);
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER1((ZERO, ZERO, ZERO, TEX0), (ZERO, ZERO, ZERO, TEX0)));
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (1cycle mode, dynamic mode)");

    {
        surface_clear(&fb, 0xFF);
        rspq_block_begin();
        rdpq_set_other_modes_raw(SOM_CYCLE_COPY);
        rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
        rspq_block_t *block = rspq_block_end();
        DEFER(rspq_block_free(block));
        rspq_block_run(block);
        rspq_wait();
        ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
            "Wrong data in framebuffer (copy mode, static mode)");
    }

    {
        surface_clear(&fb, 0xFF);
        rspq_block_begin();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER1((ZERO, ZERO, ZERO, TEX0), (ZERO, ZERO, ZERO, TEX0)));
        // rdpq_set_other_modes(SOM_CYCLE_1 | SOM_RGBDITHER_NONE | SOM_ALPHADITHER_NONE | SOM_TC_FILTER | SOM_BLENDING | SOM_SAMPLE_1X1 | SOM_MIDTEXEL);
        // rdpq_set_combine_mode(Comb_Rgb(ZERO, ZERO, ZERO, TEX0) | Comb_Alpha(ZERO, ZERO, ZERO, TEX0));
        rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
        rspq_block_t *block = rspq_block_end();
        DEFER(rspq_block_free(block));
        rspq_block_run(block);
        rspq_wait();
        ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
            "Wrong data in framebuffer (1cycle mode, static mode)");
    }

    #undef TEST_RDPQ_TEXWIDTH
    #undef TEST_RDPQ_TEXAREA
    #undef TEST_RDPQ_TEXSIZE
}

void test_rdpq_lookup_address(TestContext *ctx)
{
    RDPQ_INIT();

    const int WIDTH = 16;
    surface_t fb = surface_alloc(FMT_RGBA16, WIDTH, WIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    const color_t TEST_COLOR = RGBA32(0xFF,0xFF,0xFF,0xFF);

    uint16_t expected_fb[WIDTH*WIDTH];
    memset(expected_fb, 0xFF, sizeof(expected_fb));

    rdpq_set_mode_fill(TEST_COLOR);

    surface_clear(&fb, 0);
    rspq_block_begin();
    rdpq_set_color_image_raw(1, 0, FMT_RGBA16, WIDTH, WIDTH, WIDTH * 2);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_block_t *block = rspq_block_end();
    DEFER(rspq_block_free(block));
    rdpq_set_lookup_address(1, fb.buffer);
    rspq_block_run(block);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, 
            "Wrong data in framebuffer (static mode)");

    surface_clear(&fb, 0);
    rdpq_set_lookup_address(1, fb.buffer);
    rdpq_set_color_image_raw(1, 0, FMT_RGBA16, WIDTH, WIDTH, WIDTH * 2);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, 
            "Wrong data in framebuffer (dynamic mode)");
}

void test_rdpq_lookup_address_offset(TestContext *ctx)
{
    RDPQ_INIT();

    const int WIDTH = 16;
    surface_t fb = surface_alloc(FMT_RGBA16, WIDTH, WIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    #define TEST_RDPQ_RECT_OFF   4
    #define TEST_RDPQ_RECT_WIDTH (WIDTH-(TEST_RDPQ_RECT_OFF*2))

    const color_t TEST_COLOR = RGBA32(0xFF,0xFF,0xFF,0xFF);

    uint16_t expected_fb[WIDTH*WIDTH];
    memset(expected_fb, 0, sizeof(expected_fb));
    for (int y=TEST_RDPQ_RECT_OFF;y<WIDTH-TEST_RDPQ_RECT_OFF;y++) {
        for (int x=TEST_RDPQ_RECT_OFF;x<WIDTH-TEST_RDPQ_RECT_OFF;x++) {
            expected_fb[y * WIDTH + x] = color_to_packed16(TEST_COLOR);
        }
    }    

    rdpq_set_mode_fill(TEST_COLOR);

    uint32_t offset = (TEST_RDPQ_RECT_OFF * WIDTH + TEST_RDPQ_RECT_OFF) * 2;

    surface_clear(&fb, 0);
    rspq_block_begin();
    rdpq_set_color_image_raw(1, offset, FMT_RGBA16, TEST_RDPQ_RECT_WIDTH, TEST_RDPQ_RECT_WIDTH, WIDTH * 2);
    rdpq_fill_rectangle(0, 0, TEST_RDPQ_RECT_WIDTH, TEST_RDPQ_RECT_WIDTH);
    rspq_block_t *block = rspq_block_end();
    DEFER(rspq_block_free(block));
    rdpq_set_lookup_address(1, fb.buffer);
    rspq_block_run(block);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, 
            "Wrong data in framebuffer (static mode)");

    surface_clear(&fb, 0);
    rdpq_set_lookup_address(1, fb.buffer);
    rdpq_set_color_image_raw(1, offset, FMT_RGBA16, TEST_RDPQ_RECT_WIDTH, TEST_RDPQ_RECT_WIDTH, WIDTH * 2);
    rdpq_fill_rectangle(0, 0, WIDTH, WIDTH);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, WIDTH*WIDTH*2, 
            "Wrong data in framebuffer (dynamic mode)");

    #undef TEST_RDPQ_RECT_OFF
    #undef TEST_RDPQ_RECT_WIDTH
}

void test_rdpq_syncfull(TestContext *ctx)
{
    RDPQ_INIT();

    volatile int cb_called = 0;
    volatile uint32_t cb_value = 0;
    void cb1(void *arg1) {
        cb_called += 1;
        cb_value = (uint32_t)arg1 & 0x0000FFFF;
    }
    void cb2(void *arg1) {
        cb_called += 2;
        cb_value = (uint32_t)arg1 & 0xFFFF0000;
    }

    rdpq_sync_full(cb1, (void*)0x12345678);
    rdpq_sync_full(cb2, (void*)0xABCDEF01);
    rspq_wait();

    ASSERT_EQUAL_SIGNED(cb_called, 3, "sync full callback not called");
    ASSERT_EQUAL_HEX(cb_value, 0xABCD0000, "sync full callback wrong argument");

    rspq_block_begin();
    rdpq_sync_full(cb2, (void*)0xABCDEF01);
    rdpq_sync_full(cb1, (void*)0x12345678);
    rspq_block_t *block = rspq_block_end();
    DEFER(rspq_block_free(block));

    rspq_block_run(block);
    rspq_wait();

    ASSERT_EQUAL_SIGNED(cb_called, 6, "sync full callback not called");
    ASSERT_EQUAL_HEX(cb_value, 0x00005678, "sync full callback wrong argument");
}

static void __test_rdpq_autosyncs(TestContext *ctx, void (*func)(void), uint8_t exp[4], bool use_block) {
    // Force clearing of RDP static buffers, so that we have an easier time inspecting them.
    __rdpq_zero_blocks = true;
    DEFER(__rdpq_zero_blocks = false);

    RDPQ_INIT();

    const int WIDTH = 64;
    surface_t fb = surface_alloc(FMT_RGBA16, WIDTH, WIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    rspq_block_t *block = NULL;
    DEFER(if (block) rspq_block_free(block));

    rdpq_set_color_image(&fb);

    if (use_block) {
        rspq_block_begin();
        func();
        block = rspq_block_end();
        ASSERT(block->rdp_block, "rdpq block is empty?");
        rspq_block_run(block);
    }

    // Execute the provided function (also after the block, if requested).
    // This allows us also to get coverage of the post-block autosync state    
    func();
    rspq_wait();

    uint8_t cnt[4] = {0};
    void count_syncs(uint64_t *cmds, int n) {
        for (int i=0;i<n;i++) {
            uint8_t cmd = cmds[i] >> 56;
            if (cmd == RDPQ_CMD_SYNC_LOAD+0xC0) cnt[0]++;
            if (cmd == RDPQ_CMD_SYNC_TILE+0xC0) cnt[1]++;
            if (cmd == RDPQ_CMD_SYNC_PIPE+0xC0) cnt[2]++;
            if (cmd == RDPQ_CMD_SYNC_FULL+0xC0) cnt[3]++;            
        }
    }

    // Pointer to RDP primitives in dynamic buffer. Normally, the current
    // buffer is the one with index 0.
    // If we went through a block, RSPQ_RdpSend has already swapped the
    // two buffers so the one we are interested into is the 1.
    extern void *rspq_rdp_dynamic_buffers[2];
    uint64_t *rdp_cmds = use_block ? rspq_rdp_dynamic_buffers[1] : rspq_rdp_dynamic_buffers[0];
    if (use_block) {
        rdpq_block_t *bb = block->rdp_block;
        int size = RDPQ_BLOCK_MIN_SIZE * 4;
        while (bb) {
            count_syncs((uint64_t*)bb->cmds, size / 8);
            bb = bb->next;
            size *= 2;
        }
    }
    
    count_syncs(rdp_cmds, 32);
    ASSERT_EQUAL_MEM(cnt, exp, 4, "Unexpected sync commands");
}

static void __autosync_pipe1(void) {
    rdpq_set_other_modes_raw(SOM_CYCLE_FILL);
    rdpq_set_fill_color(RGBA32(0,0,0,0));
    rdpq_fill_rectangle(0, 0, 8, 8);
    // PIPESYNC HERE
    rdpq_set_other_modes_raw(SOM_CYCLE_FILL);
    rdpq_fill_rectangle(0, 0, 8, 8);
    // NO PIPESYNC HERE
    rdpq_set_prim_color(RGBA32(1,1,1,1));
    // NO PIPESYNC HERE
    rdpq_set_prim_depth(0, 1);
    // NO PIPESYNC HERE
    rdpq_set_scissor(0,0,1,1);
    rdpq_fill_rectangle(0, 0, 8, 8);
}
static uint8_t __autosync_pipe1_exp[4] = {0,0,1,1};
static uint8_t __autosync_pipe1_blockexp[4] = {0,0,4,1};

static void __autosync_tile1(void) {
    rdpq_set_tile(0, FMT_RGBA16, 0, 128, 0);
    rdpq_set_tile_size(0, 0, 0, 16, 16);
    rdpq_texture_rectangle(0, 0, 0, 4, 4, 0, 0, 1, 1);    
    // NO TILESYNC HERE
    rdpq_set_tile(1, FMT_RGBA16, 0, 128, 0);
    rdpq_set_tile_size(1, 0, 0, 16, 16);
    rdpq_texture_rectangle(1, 0, 0, 4, 4, 0, 0, 1, 1);    
    rdpq_set_tile(2, FMT_RGBA16, 0, 128, 0);
    rdpq_set_tile_size(2, 0, 0, 16, 16);
    // NO TILESYNC HERE
    rdpq_set_tile(2, FMT_RGBA16, 0, 256, 0);
    // NO TILESYNC HERE
    rdpq_texture_rectangle(1, 0, 0, 4, 4, 0, 0, 1, 1);    
    rdpq_texture_rectangle(0, 0, 0, 4, 4, 0, 0, 1, 1);    
    // TILESYNC HERE
    rdpq_set_tile(1, FMT_RGBA16, 0, 256, 0);
    rdpq_set_tile_size(1, 0, 0, 16, 16);
    rdpq_texture_rectangle(1, 0, 0, 4, 4, 0, 0, 1, 1);    
    // TILESYNC HERE
    rdpq_set_tile_size(1, 0, 0, 32, 32);

}
static uint8_t __autosync_tile1_exp[4] = {0,2,0,1};
static uint8_t __autosync_tile1_blockexp[4] = {0,5,0,1};

static void __autosync_load1(void) {
    surface_t tex = surface_alloc(FMT_I8, 8, 8);
    DEFER(surface_free(&tex));

    rdpq_set_texture_image(&tex);
    rdpq_set_tile(0, FMT_RGBA16, 0, 128, 0);
    // NO LOADSYNC HERE
    rdpq_load_tile(0, 0, 0, 7, 7);
    rdpq_set_tile(1, FMT_RGBA16, 0, 128, 0);
    // NO LOADSYNC HERE
    rdpq_load_tile(1, 0, 0, 7, 7);
    // NO LOADSYNC HERE
    rdpq_texture_rectangle(1, 0, 0, 4, 4, 0, 0, 1, 1);
    // LOADSYNC HERE
    rdpq_load_tile(0, 0, 0, 7, 7);
}
static uint8_t __autosync_load1_exp[4] = {1,0,0,1};
static uint8_t __autosync_load1_blockexp[4] = {3,2,2,1};

void test_rdpq_autosync(TestContext *ctx) {
    LOG("__autosync_pipe1\n");
    __test_rdpq_autosyncs(ctx, __autosync_pipe1, __autosync_pipe1_exp, false);
    if (ctx->result == TEST_FAILED) return;

    LOG("__autosync_pipe1 (block)\n");
    __test_rdpq_autosyncs(ctx, __autosync_pipe1, __autosync_pipe1_blockexp, true);
    if (ctx->result == TEST_FAILED) return;

    LOG("__autosync_tile1\n");
    __test_rdpq_autosyncs(ctx, __autosync_tile1, __autosync_tile1_exp, false);
    if (ctx->result == TEST_FAILED) return;

    LOG("__autosync_tile1 (block)\n");
    __test_rdpq_autosyncs(ctx, __autosync_tile1, __autosync_tile1_blockexp, true);
    if (ctx->result == TEST_FAILED) return;

    LOG("__autosync_load1\n");
    __test_rdpq_autosyncs(ctx, __autosync_load1, __autosync_load1_exp, false);
    if (ctx->result == TEST_FAILED) return;

    LOG("__autosync_load1 (block)\n");
    __test_rdpq_autosyncs(ctx, __autosync_load1, __autosync_load1_blockexp, true);
    if (ctx->result == TEST_FAILED) return;
}


void test_rdpq_automode(TestContext *ctx) {
    RDPQ_INIT();

    const int FBWIDTH = 16;
    surface_t fb = surface_alloc(FMT_RGBA16, FBWIDTH, FBWIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    const int TEXWIDTH = FBWIDTH - 8;
    surface_t tex = surface_alloc(FMT_RGBA16, TEXWIDTH, TEXWIDTH);
    DEFER(surface_free(&tex));
    surface_clear(&tex, 0);

    uint16_t expected_fb[FBWIDTH*FBWIDTH];
    memset(expected_fb, 0xFF, sizeof(expected_fb));
    for (int y=0;y<TEXWIDTH;y++) {
        for (int x=0;x<TEXWIDTH;x++) {
            color_t c = RGBA16(RANDN(32), RANDN(32), RANDN(32), 1);
            expected_fb[(y + 4) * FBWIDTH + (x + 4)] = color_to_packed16(c);
            ((uint16_t*)tex.buffer)[y * TEXWIDTH + x] = color_to_packed16(c);
        }
    }

    uint64_t som;
    rdpq_set_color_image(&fb);
    rdpq_set_texture_image(&tex);
    rdpq_set_tile(0, FMT_RGBA16, 0, TEXWIDTH * 2, 0);
    rdpq_set_tile(1, FMT_RGBA16, 0, TEXWIDTH * 2, 0);
    rdpq_load_tile(0, 0, 0, TEXWIDTH, TEXWIDTH);
    rdpq_load_tile(1, 0, 0, TEXWIDTH, TEXWIDTH);
    rdpq_set_mode_standard();
    rdpq_set_blend_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
    rdpq_set_fog_color(RGBA32(0xEE, 0xEE, 0xEE, 0xFF));
    rdpq_set_env_color(RGBA32(0x0,0x0,0x0,0x7F));
    rdpq_set_prim_color(RGBA32(0x0,0x0,0x0,0x7F));

    // Set simple 1-pass combiner => 1 cycle
    surface_clear(&fb, 0xFF);
    rdpq_mode_combiner(RDPQ_COMBINER1((ZERO, ZERO, ZERO, TEX0), (ZERO, ZERO, ZERO, ZERO)));
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
    rspq_wait();
    som = rdpq_get_other_modes_raw();
    ASSERT_EQUAL_HEX(som & SOM_CYCLE_MASK, SOM_CYCLE_1, "invalid cycle type");
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (comb=1pass, blender=off)");

    // Activate blending (1-pass blender) => 1 cycle
    surface_clear(&fb, 0xFF);
    rdpq_mode_blending(RDPQ_BLENDER((IN_RGB, FOG_ALPHA, BLEND_RGB, INV_MUX_ALPHA)));
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
    rspq_wait();
    som = rdpq_get_other_modes_raw();
    ASSERT_EQUAL_HEX(som & SOM_CYCLE_MASK, SOM_CYCLE_1, "invalid cycle type");
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (comb=1pass, blender=1pass)");

    // Activate fogging (2-pass blender) => 2 cycle
    surface_clear(&fb, 0xFF);
    rdpq_mode_fog(RDPQ_BLENDER((BLEND_RGB, ZERO, IN_RGB, INV_MUX_ALPHA)));
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
    rspq_wait();
    som = rdpq_get_other_modes_raw();
    ASSERT_EQUAL_HEX(som & SOM_CYCLE_MASK, SOM_CYCLE_2, "invalid cycle type");
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (comb=1pass, blender=2pass)");

    // Set two-pass combiner => 2 cycle
    surface_clear(&fb, 0xFF);
    rdpq_mode_combiner(RDPQ_COMBINER2(
        (ZERO, ZERO, ZERO, ENV), (ENV, ZERO, TEX0, PRIM),
        (TEX1, ZERO, COMBINED_ALPHA, ZERO), (ZERO, ZERO, ZERO, ZERO)));
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
    rspq_wait();    
    som = rdpq_get_other_modes_raw();
    ASSERT_EQUAL_HEX(som & SOM_CYCLE_MASK, SOM_CYCLE_2, "invalid cycle type");
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (comb=2pass, blender=2pass)");

    // Disable fogging (1 pass blender) => 2 cycle
    surface_clear(&fb, 0xFF);
    rdpq_mode_fog(0);
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
    rspq_wait();
    som = rdpq_get_other_modes_raw();
    ASSERT_EQUAL_HEX(som & SOM_CYCLE_MASK, SOM_CYCLE_2, "invalid cycle type");
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (comb=2pass, blender=1pass)");

    // Set simple combiner => 1 cycle
    surface_clear(&fb, 0xFF);
    rdpq_mode_combiner(RDPQ_COMBINER1((ZERO, ZERO, ZERO, TEX0), (ZERO, ZERO, ZERO, ZERO)));
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
    rspq_wait();
    som = rdpq_get_other_modes_raw();
    ASSERT_EQUAL_HEX(som & SOM_CYCLE_MASK, SOM_CYCLE_1, "invalid cycle type");
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (comb=1pass, blender=1pass)");

    // Push the current mode, then modify several states, then pop.
    rdpq_mode_push();
    rdpq_mode_combiner(RDPQ_COMBINER2(
        (ZERO, ZERO, ZERO, TEX0), (ZERO, ZERO, ZERO, ZERO),
        (COMBINED, ZERO, ZERO, TEX1), (ZERO, ZERO, ZERO, ZERO)
    ));
    rdpq_mode_blending(RDPQ_BLENDER((IN_RGB, ZERO, BLEND_RGB, ONE)));
    rdpq_mode_dithering(DITHER_NOISE, DITHER_NOISE);
    rdpq_mode_pop();
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1, 1);
    rspq_wait();
    som = rdpq_get_other_modes_raw();
    ASSERT_EQUAL_HEX(som & SOM_CYCLE_MASK, SOM_CYCLE_1, "invalid cycle type");
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (comb=1pass, blender=1pass (after pop))");
}

void test_rdpq_blender(TestContext *ctx) {
    RDPQ_INIT();

    const int FBWIDTH = 16;
    surface_t fb = surface_alloc(FMT_RGBA16, FBWIDTH, FBWIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    const int TEXWIDTH = FBWIDTH;
    surface_t tex = surface_alloc(FMT_RGBA16, TEXWIDTH, TEXWIDTH);
    DEFER(surface_free(&tex));
    surface_clear(&tex, 0xAA);

    const color_t BLEND_COLOR = RGBA32(0x30, 0x30, 0x30, 0xFF);
    const color_t BLEND_COLOR2 = RGBA32(0x30*2-1, 0x30*2-1, 0x30*2-1, 0xFF);

    uint16_t expected_fb_blend[FBWIDTH*FBWIDTH], expected_fb_blend2[FBWIDTH*FBWIDTH], expected_fb_tex[FBWIDTH*FBWIDTH];
    memset(expected_fb_blend, 0, sizeof(expected_fb_blend));
    memset(expected_fb_blend2, 0, sizeof(expected_fb_blend2));
    memset(expected_fb_tex, 0, sizeof(expected_fb_tex));
    for (int y=4;y<FBWIDTH-4;y++) {
        for (int x=4;x<FBWIDTH-4;x++) {
            expected_fb_blend[y * FBWIDTH + x] = color_to_packed16(BLEND_COLOR);
            expected_fb_blend2[y * FBWIDTH + x] = color_to_packed16(BLEND_COLOR2);
            expected_fb_tex[y * FBWIDTH + x] = 0xAAAA | 1;
        }
    }

    rdpq_set_color_image(&fb);
    rdpq_set_texture_image(&tex);
    rdpq_set_tile(0, FMT_RGBA16, 0, TEXWIDTH * 2, 0);
    rdpq_set_tile(1, FMT_RGBA16, 0, TEXWIDTH * 2, 0);
    rdpq_load_tile(0, 0, 0, TEXWIDTH, TEXWIDTH);
    rdpq_load_tile(1, 0, 0, TEXWIDTH, TEXWIDTH);

    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER1((ZERO, ZERO, ZERO, TEX0), (ZERO, ZERO, ZERO, TEX0)));
    rdpq_set_blend_color(BLEND_COLOR);
    rdpq_set_fog_color(RGBA32(0xEE, 0xEE, 0xEE, 0xFF));

    // Enable blending
    rdpq_mode_blending(RDPQ_BLENDER((IN_RGB, ZERO, BLEND_RGB, INV_MUX_ALPHA)));
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1.0f, 1.0f);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb_blend, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (blender=pass1)");

    // Disable blending
    rdpq_mode_blending(0);
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1.0f, 1.0f);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb_tex, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (blender=none)");

    // Enable fogging
    rdpq_mode_fog(RDPQ_BLENDER((IN_RGB, ZERO, BLEND_RGB, INV_MUX_ALPHA)));
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1.0f, 1.0f);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb_blend, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (blender=pass0)");

    // Disable fogging
    rdpq_mode_fog(0);
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1.0f, 1.0f);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb_tex, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (blender=none)");

    // Enable both, with blending adding the input of fog
    rdpq_mode_fog(RDPQ_BLENDER((IN_RGB, ZERO, BLEND_RGB, INV_MUX_ALPHA)));
    rdpq_mode_blending(RDPQ_BLENDER((IN_RGB, FOG_ALPHA, BLEND_RGB, ONE)));
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1.0f, 1.0f);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb_blend2, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (blender=pass0+1)");

    // Disable blend
    rdpq_mode_blending(0);
    rdpq_texture_rectangle(0, 4, 4, FBWIDTH-4, FBWIDTH-4, 0, 0, 1.0f, 1.0f);
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb_blend, FBWIDTH*FBWIDTH*2, 
        "Wrong data in framebuffer (blender=pass0)");
}

void test_rdpq_blender_memory(TestContext *ctx) {
    RDPQ_INIT();

    const int FBWIDTH = 16;
    surface_t fb = surface_alloc(FMT_RGBA32, FBWIDTH, FBWIDTH);
    DEFER(surface_free(&fb));

    uint32_t expected_fb[FBWIDTH*FBWIDTH];

    for (int j=0;j<FBWIDTH;j++) {
        for (int i=0;i<FBWIDTH;i++) {
            bool alt = (i % (j/2+1) < 3);
            ((uint32_t*)fb.buffer)[j * FBWIDTH + i] = alt ? 0xB0B0B080 : 0x30303080;
            if (i >= 4 && i < 12 && j >= 4 && j <12)
                expected_fb[j * FBWIDTH + i] = alt ? 0x989898e0 : 0x585858e0;
            else
                expected_fb[j * FBWIDTH + i] = alt ? 0xB0B0B080 : 0x30303080;
        }
    }

    const int TEXWIDTH = 8;
    surface_t tex = surface_alloc(FMT_RGBA32, TEXWIDTH, TEXWIDTH);
    DEFER(surface_free(&tex));
    surface_clear(&tex, 0x80);

    rdpq_set_fog_color(RGBA32(0,0,0,0x80));
    rdpq_set_color_image(&fb);
    rdpq_tex_load(TILE0, &tex, 0);
    rdpq_set_mode_standard();
    rdpq_mode_blending(RDPQ_BLEND_MULTIPLY);
    rdpq_triangle(TILE0, 0, 0, -1, 2, 0,
        (float[]){ 4.0f,   4.0f, 0.0f, 0.0f, 1.0f },
        (float[]){ 12.0f,  4.0f, 8.0f, 0.0f, 1.0f },
        (float[]){ 12.0f, 12.0f, 8.0f, 8.0f, 1.0f }
    );
    rdpq_triangle(TILE0, 0, 0, -1, 2, -1,
        (float[]){ 4.0f,   4.0f, 0.0f, 0.0f },
        (float[]){ 4.0f,  12.0f, 0.0f, 8.0f },
        (float[]){ 12.0f, 12.0f, 8.0f, 8.0f }
    );
    rspq_wait();
    ASSERT_EQUAL_MEM((uint8_t*)fb.buffer, (uint8_t*)expected_fb, FBWIDTH*FBWIDTH*4, "Wrong data in framebuffer");
    uint64_t som = rdpq_get_other_modes_raw();
    ASSERT_EQUAL_HEX(som & SOM_CYCLE_MASK, SOM_CYCLE_1, "invalid cycle type");
}


void test_rdpq_tex_load(TestContext *ctx) {
    RDPQ_INIT();

    const int FBWIDTH = 16;
    surface_t fb = surface_alloc(FMT_RGBA16, FBWIDTH, FBWIDTH);
    DEFER(surface_free(&fb));
    surface_clear(&fb, 0);

    const int TEXWIDTH = 16;
    surface_t tex = surface_alloc(FMT_CI4, TEXWIDTH, TEXWIDTH);
    DEFER(surface_free(&tex));

    // surface_t sub = surface_make_sub(&tex, 4, 4, 8, 8);
    uint16_t* tlut = malloc_uncached(256*2);

    for (int i=0;i<256;i++) {
        tlut[i] = (i<<1)|1;
    }
    for (int j=0;j<TEXWIDTH;j++) {
        for (int i=0;i<TEXWIDTH/2;i++) {
            ((uint8_t*)tex.buffer)[j * TEXWIDTH/2 + i] = 
                (((j+i*2)&15)<<4) | ((j+i*2+1)&15);
        }
    }

    rdpq_set_color_image(&fb);
    rdpq_set_mode_standard();
    rdpq_tex_load_ci4(0, &tex, 0, 4);
    rdpq_tex_load_tlut(tlut, 0, 256);
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_texture_rectangle(0, 0, 0, 16, 16, 0, 0, 1.0f, 1.0f);
    rspq_wait();

    debug_surface("Found:", fb.buffer, FBWIDTH, FBWIDTH);

    surface_t tmem = rdpq_debug_get_tmem();
    debugf("TMEM:\n");
    debugf_hexdump(tmem.buffer, 4096);
    surface_free(&tmem);
}