#include "titan_asset_image.h"

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>
#include <png.h>

#define TITAN_IMAGE_MAX_WIDTH 4096u
#define TITAN_IMAGE_MAX_HEIGHT 3072u
#define TITAN_IMAGE_MAX_PIXELS (6u * 1024u * 1024u)
#define TITAN_TIMG_VERSION 2u
#define TITAN_TIMG_FLAG_PSM16 1u
#define TITAN_IMAGE_MEDIA_WIDTH 512u
#define TITAN_IMAGE_MEDIA_HEIGHT 256u

typedef struct TitanImageTarget
{
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int offset_x;
    unsigned int offset_y;
    unsigned int crop_x;
    unsigned int crop_y;
    unsigned int crop_w;
    unsigned int crop_h;
    unsigned int xmap[TITAN_IMAGE_MEDIA_WIDTH];
    unsigned int ymap[TITAN_IMAGE_MEDIA_HEIGHT];
    unsigned short *pixels;
} TitanImageTarget;

static void TitanAssetImage_WriteLe16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8u) & 0xffu);
}

static void TitanAssetImage_WriteLe32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8u) & 0xffu);
    p[2] = (unsigned char)((v >> 16u) & 0xffu);
    p[3] = (unsigned char)((v >> 24u) & 0xffu);
}

static uint32_t TitanAssetImage_Crc32(const unsigned char *data, unsigned int size)
{
    uint32_t crc = 0xffffffffu;
    unsigned int i;
    for (i = 0u; i < size; i++)
    {
        unsigned int bit;
        crc ^= data[i];
        for (bit = 0u; bit < 8u; bit++)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static unsigned short TitanAssetImage_PackPsm16(unsigned int r, unsigned int g,
    unsigned int b, unsigned int a)
{
    return (unsigned short)(
        ((r >> 3u) & 0x1fu) |
        (((g >> 3u) & 0x1fu) << 5u) |
        (((b >> 3u) & 0x1fu) << 10u) |
        (a >= 128u ? 0x8000u : 0u)
    );
}

static int TitanAssetImage_PrepareTarget(TitanImageTarget *target,
    unsigned int source_width, unsigned int source_height, TitanAssetKind kind,
    unsigned short *pixels)
{
    unsigned int i;
    uint64_t lhs;
    uint64_t rhs;
    unsigned int canvas_width;
    unsigned int canvas_height;
    int fit_inside = kind == TITAN_ASSET_KIND_APP_MEDIA;
    if (target == 0 || pixels == 0 || source_width == 0u || source_height == 0u ||
        source_width > TITAN_IMAGE_MAX_WIDTH || source_height > TITAN_IMAGE_MAX_HEIGHT ||
        (uint64_t)source_width * (uint64_t)source_height > TITAN_IMAGE_MAX_PIXELS)
        return 0;
    memset(target, 0, sizeof(*target));
    if (kind == TITAN_ASSET_KIND_ICON)
    {
        canvas_width = TITAN_ASSET_ICON_SOURCE_WIDTH;
        canvas_height = TITAN_ASSET_ICON_SOURCE_HEIGHT;
    }
    else if (kind == TITAN_ASSET_KIND_BACKGROUND)
    {
        canvas_width = TITAN_ASSET_BACKGROUND_WIDTH;
        canvas_height = TITAN_ASSET_BACKGROUND_HEIGHT;
    }
    else if (kind == TITAN_ASSET_KIND_APP_MEDIA)
    {
        canvas_width = TITAN_IMAGE_MEDIA_WIDTH;
        canvas_height = TITAN_IMAGE_MEDIA_HEIGHT;
    }
    else return 0;
    target->stride = canvas_width;
    target->pixels = pixels;
    target->crop_x = 0u;
    target->crop_y = 0u;
    target->crop_w = source_width;
    target->crop_h = source_height;
    lhs = (uint64_t)source_width * (uint64_t)canvas_height;
    rhs = (uint64_t)source_height * (uint64_t)canvas_width;
    if (fit_inside)
    {
        if (lhs > rhs)
        {
            target->width = canvas_width;
            target->height = (unsigned int)(((uint64_t)source_height * canvas_width) / source_width);
            if (target->height == 0u) target->height = 1u;
            target->offset_y = (canvas_height - target->height) / 2u;
        }
        else
        {
            target->height = canvas_height;
            target->width = (unsigned int)(((uint64_t)source_width * canvas_height) / source_height);
            if (target->width == 0u) target->width = 1u;
            target->offset_x = (canvas_width - target->width) / 2u;
        }
    }
    else
    {
        target->width = canvas_width;
        target->height = canvas_height;
        if (lhs > rhs)
        {
            target->crop_h = source_height;
            target->crop_w = (unsigned int)(((uint64_t)source_height * target->width) / target->height);
            if (target->crop_w == 0u || target->crop_w > source_width) return 0;
            target->crop_x = (source_width - target->crop_w) / 2u;
        }
        else
        {
            target->crop_w = source_width;
            target->crop_h = (unsigned int)(((uint64_t)source_width * target->height) / target->width);
            if (target->crop_h == 0u || target->crop_h > source_height) return 0;
            target->crop_y = (source_height - target->crop_h) / 2u;
        }
    }
    for (i = 0u; i < target->width; i++)
        target->xmap[i] = target->crop_x +
            (unsigned int)(((uint64_t)i * target->crop_w) / target->width);
    for (i = 0u; i < target->height; i++)
        target->ymap[i] = target->crop_y +
            (unsigned int)(((uint64_t)i * target->crop_h) / target->height);
    return 1;
}

static void TitanAssetImage_ConsumeRgbaRow(const TitanImageTarget *target,
    const unsigned char *row, unsigned int source_y, unsigned int channels)
{
    unsigned int out_y;
    for (out_y = 0u; out_y < target->height; out_y++)
    {
        unsigned int out_x;
        if (target->ymap[out_y] != source_y) continue;
        for (out_x = 0u; out_x < target->width; out_x++)
        {
            unsigned int sx = target->xmap[out_x];
            const unsigned char *p = row + sx * channels;
            unsigned int a = channels >= 4u ? p[3] : 255u;
            target->pixels[(target->offset_y + out_y) * target->stride +
                target->offset_x + out_x] =
                TitanAssetImage_PackPsm16(p[0], p[1], p[2], a);
        }
    }
}

typedef struct TitanPngMemory
{
    const unsigned char *data;
    size_t size;
    size_t offset;
} TitanPngMemory;

static void TitanAssetImage_PngRead(png_structp png_ptr, png_bytep out, png_size_t length)
{
    TitanPngMemory *memory = (TitanPngMemory *)png_get_io_ptr(png_ptr);
    if (memory == 0 || memory->offset > memory->size ||
        length > memory->size - memory->offset)
        png_error(png_ptr, "Titan PNG input truncated");
    memcpy(out, memory->data + memory->offset, length);
    memory->offset += length;
}

static int TitanAssetImage_Cooperate(const TitanAssetImageOptions *options, unsigned int row)
{
    unsigned int interval;
    if (options == 0) return 1;
    if (options->should_cancel != 0 && options->should_cancel(options->user_data)) return 0;
    interval = options->yield_every_rows != 0u ? options->yield_every_rows : 8u;
    if (options->yield != 0 && (row % interval) == 0u) options->yield(options->user_data);
    return options->should_cancel == 0 || !options->should_cancel(options->user_data);
}

static int TitanAssetImage_DecodePng(const unsigned char *source, unsigned int source_size,
    TitanAssetKind kind, unsigned short *pixels, const TitanAssetImageOptions *options)
{
    png_structp png_ptr = 0;
    png_infop info_ptr = 0;
    TitanPngMemory memory;
    TitanImageTarget target;
    unsigned char *row = 0;
    unsigned char *display_rows = 0;
    png_uint_32 width, height;
    int bit_depth, color_type;
    int interlace_passes = 1;
    int pass;
    png_size_t rowbytes;
    unsigned int y;
    int ok = 0;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
    if (png_ptr == 0) return 0;
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == 0) { png_destroy_read_struct(&png_ptr, 0, 0); return 0; }
    if (setjmp(png_jmpbuf(png_ptr))) goto done;
    memory.data = source; memory.size = source_size; memory.offset = 0u;
    png_set_read_fn(png_ptr, &memory, TitanAssetImage_PngRead);
    png_read_info(png_ptr, info_ptr);
    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
    if (width == 0u || height == 0u || width > TITAN_IMAGE_MAX_WIDTH || height > TITAN_IMAGE_MAX_HEIGHT ||
        (uint64_t)width * (uint64_t)height > TITAN_IMAGE_MAX_PIXELS) goto done;
    if (options != 0 &&
        ((options->minimum_source_width != 0u && width < options->minimum_source_width) ||
         (options->minimum_source_height != 0u && height < options->minimum_source_height))) goto done;
    /* Adam7 remains bounded. For interlaced input we retain only the source
     * rows that map to the final 128/256x128 target, never the full image. */
    interlace_passes = png_set_interlace_handling(png_ptr);
    if (interlace_passes < 1 || interlace_passes > 7) goto done;
    if (bit_depth == 16) png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);
    if (!(color_type & PNG_COLOR_MASK_ALPHA) && !png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_filler(png_ptr, 0xffu, PNG_FILLER_AFTER);
    png_read_update_info(png_ptr, info_ptr);
    if (png_get_channels(png_ptr, info_ptr) != 4) goto done;
    rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    if (rowbytes != (png_size_t)width * 4u ||
        rowbytes > (png_size_t)TITAN_IMAGE_MAX_WIDTH * 4u) goto done;
    if (!TitanAssetImage_PrepareTarget(&target, (unsigned int)width, (unsigned int)height, kind, pixels)) goto done;
    row = (unsigned char *)malloc((size_t)rowbytes);
    if (row == 0) goto done;
    if (interlace_passes > 1)
    {
        size_t cache_bytes = (size_t)target.height * (size_t)rowbytes;
        if (target.height != 0u && cache_bytes / (size_t)target.height != (size_t)rowbytes) goto done;
        display_rows = (unsigned char *)calloc(1u, cache_bytes);
        if (display_rows == 0) goto done;
    }
    for (pass = 0; pass < interlace_passes; pass++)
    {
        for (y = 0u; y < (unsigned int)height; y++)
        {
            unsigned int cooperate_row = (unsigned int)pass * (unsigned int)height + y;
            unsigned int out_y;
            unsigned char *display = 0;
            if (!TitanAssetImage_Cooperate(options, cooperate_row)) goto done;
            memset(row, 0, (size_t)rowbytes);
            if (interlace_passes > 1)
            {
                for (out_y = 0u; out_y < target.height; out_y++)
                {
                    if (target.ymap[out_y] == y)
                    {
                        display = display_rows + (size_t)out_y * (size_t)rowbytes;
                        break;
                    }
                }
                png_read_row(png_ptr, row, display);
                if (pass + 1 == interlace_passes && display != 0)
                    TitanAssetImage_ConsumeRgbaRow(&target, display, y, 4u);
            }
            else
            {
                png_read_row(png_ptr, row, 0);
                TitanAssetImage_ConsumeRgbaRow(&target, row, y, 4u);
            }
        }
    }
    if (!TitanAssetImage_Cooperate(options, (unsigned int)interlace_passes * (unsigned int)height)) goto done;
    png_read_end(png_ptr, info_ptr);
    ok = 1;
done:
    free(display_rows);
    free(row);
    png_destroy_read_struct(&png_ptr, &info_ptr, 0);
    return ok;
}

typedef struct TitanJpegError
{
    struct jpeg_error_mgr pub;
    jmp_buf jump;
} TitanJpegError;

static void TitanAssetImage_JpegError(j_common_ptr cinfo)
{
    TitanJpegError *error = (TitanJpegError *)cinfo->err;
    longjmp(error->jump, 1);
}

static int TitanAssetImage_DecodeJpeg(const unsigned char *source, unsigned int source_size,
    TitanAssetKind kind, unsigned short *pixels, const TitanAssetImageOptions *options)
{
    struct jpeg_decompress_struct cinfo;
    TitanJpegError error;
    TitanImageTarget target;
    JSAMPARRAY row = 0;
    int created = 0;
    int ok = 0;
    unsigned int target_width;
    unsigned int target_height;
    unsigned int scale_denom;
    uint64_t source_pixels;
    cinfo.err = jpeg_std_error(&error.pub);
    error.pub.error_exit = TitanAssetImage_JpegError;
    if (setjmp(error.jump)) goto done;
    jpeg_create_decompress(&cinfo);
    created = 1;
    jpeg_mem_src(&cinfo, (const unsigned char *)source, (unsigned long)source_size);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) goto done;
    cinfo.out_color_space = JCS_RGB;
    cinfo.do_fancy_upsampling = FALSE;
    source_pixels = (uint64_t)cinfo.image_width * (uint64_t)cinfo.image_height;
    if (cinfo.image_width == 0u || cinfo.image_height == 0u ||
        cinfo.image_width > TITAN_IMAGE_MAX_WIDTH || cinfo.image_height > TITAN_IMAGE_MAX_HEIGHT ||
        source_pixels > TITAN_IMAGE_MAX_PIXELS) goto done;
    if (options != 0 &&
        ((options->minimum_source_width != 0u && cinfo.image_width < options->minimum_source_width) ||
         (options->minimum_source_height != 0u && cinfo.image_height < options->minimum_source_height))) goto done;

    if (kind == TITAN_ASSET_KIND_ICON)
    { target_width = TITAN_ASSET_ICON_SOURCE_WIDTH; target_height = TITAN_ASSET_ICON_SOURCE_HEIGHT; }
    else if (kind == TITAN_ASSET_KIND_BACKGROUND)
    { target_width = TITAN_ASSET_BACKGROUND_WIDTH; target_height = TITAN_ASSET_BACKGROUND_HEIGHT; }
    else if (kind == TITAN_ASSET_KIND_APP_MEDIA)
    { target_width = TITAN_IMAGE_MEDIA_WIDTH; target_height = TITAN_IMAGE_MEDIA_HEIGHT; }
    else goto done;

    /* JPEG supports reduced-IDCT output at 1/2, 1/4 and 1/8 resolution.
     * Decode the smallest raster that still covers the final canvas: this
     * avoids expanding millions of source pixels only to discard them during
     * thumbnail/background resampling. PNG stays on its streaming row path. */
    cinfo.scale_num = 1u;
    for (scale_denom = 8u; scale_denom >= 2u; scale_denom /= 2u)
    {
        cinfo.scale_denom = scale_denom;
        jpeg_calc_output_dimensions(&cinfo);
        if (cinfo.output_width >= target_width && cinfo.output_height >= target_height)
            break;
    }
    if (scale_denom < 2u) cinfo.scale_denom = 1u;
    jpeg_calc_output_dimensions(&cinfo);
    jpeg_start_decompress(&cinfo);
    if (cinfo.output_width == 0u || cinfo.output_height == 0u ||
        cinfo.output_width > TITAN_IMAGE_MAX_WIDTH || cinfo.output_height > TITAN_IMAGE_MAX_HEIGHT ||
        (uint64_t)cinfo.output_width * (uint64_t)cinfo.output_height > TITAN_IMAGE_MAX_PIXELS ||
        cinfo.output_components != 3) goto done;
    if (!TitanAssetImage_PrepareTarget(&target, (unsigned int)cinfo.output_width,
            (unsigned int)cinfo.output_height, kind, pixels)) goto done;
    row = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE,
        cinfo.output_width * cinfo.output_components, 1u);
    while (cinfo.output_scanline < cinfo.output_height)
    {
        unsigned int y = (unsigned int)cinfo.output_scanline;
        if (!TitanAssetImage_Cooperate(options, y)) goto done;
        if (jpeg_read_scanlines(&cinfo, row, 1u) != 1u) goto done;
        TitanAssetImage_ConsumeRgbaRow(&target, row[0], y, 3u);
    }
    if (!TitanAssetImage_Cooperate(options, (unsigned int)cinfo.output_height)) goto done;
    jpeg_finish_decompress(&cinfo);
    ok = 1;
done:
    if (created) jpeg_destroy_decompress(&cinfo);
    return ok;
}

int TitanAssetImage_ConvertToTimgEx(const unsigned char *source, unsigned int source_size,
    TitanAssetKind kind, unsigned char *destination, unsigned int destination_capacity,
    unsigned int *out_size, const TitanAssetImageOptions *options)
{
    unsigned int width;
    unsigned int height;
    unsigned int payload_size;
    unsigned int total_size;
    unsigned short *pixels;
    int ok;
    if (out_size != 0) *out_size = 0u;
    if (source == 0 || destination == 0 || source_size < 4u || out_size == 0) return 0;
    if (options != 0 && options->should_cancel != 0 && options->should_cancel(options->user_data)) return 0;
    if (kind == TITAN_ASSET_KIND_ICON)
    {
        width = TITAN_ASSET_ICON_SOURCE_WIDTH;
        height = TITAN_ASSET_ICON_SOURCE_HEIGHT;
    }
    else if (kind == TITAN_ASSET_KIND_BACKGROUND)
    {
        width = TITAN_ASSET_BACKGROUND_WIDTH;
        height = TITAN_ASSET_BACKGROUND_HEIGHT;
    }
    else return 0;
    payload_size = width * height * 2u;
    total_size = TITAN_ASSET_TIMG_HEADER_SIZE + payload_size;
    if (destination_capacity < total_size) return 0;
    memset(destination, 0, total_size);
    pixels = (unsigned short *)(void *)(destination + TITAN_ASSET_TIMG_HEADER_SIZE);

    if (source_size >= 8u && png_sig_cmp((png_const_bytep)source, 0u, 8u) == 0)
        ok = TitanAssetImage_DecodePng(source, source_size, kind, pixels, options);
    else if (source[0] == 0xffu && source[1] == 0xd8u)
        ok = TitanAssetImage_DecodeJpeg(source, source_size, kind, pixels, options);
    else return 0;
    if (!ok) return 0;

    destination[0] = 'T'; destination[1] = 'I'; destination[2] = 'M'; destination[3] = 'G';
    TitanAssetImage_WriteLe16(destination + 4u, TITAN_TIMG_VERSION);
    TitanAssetImage_WriteLe16(destination + 6u, (uint16_t)kind);
    TitanAssetImage_WriteLe16(destination + 8u, (uint16_t)width);
    TitanAssetImage_WriteLe16(destination + 10u, (uint16_t)height);
    TitanAssetImage_WriteLe32(destination + 12u, TITAN_TIMG_FLAG_PSM16);
    TitanAssetImage_WriteLe32(destination + 16u, payload_size);
    TitanAssetImage_WriteLe32(destination + 20u,
        TitanAssetImage_Crc32(destination + TITAN_ASSET_TIMG_HEADER_SIZE, payload_size));
    *out_size = total_size;
    return 1;
}

int TitanAssetImage_DecodeMediaRgb565Fit(const unsigned char *source,
    unsigned int source_size, unsigned short *destination, unsigned int width,
    unsigned int height, unsigned int stride, const TitanAssetImageOptions *options)
{
    int ok;
    if (source == 0 || destination == 0 || source_size < 4u ||
        width != TITAN_IMAGE_MEDIA_WIDTH || height != TITAN_IMAGE_MEDIA_HEIGHT ||
        stride != width)
        return 0;
    memset(destination, 0, width * height * sizeof(*destination));
    if (source_size >= 8u && png_sig_cmp((png_const_bytep)source, 0u, 8u) == 0)
        ok = TitanAssetImage_DecodePng(source, source_size, TITAN_ASSET_KIND_APP_MEDIA,
            destination, options);
    else if (source[0] == 0xffu && source[1] == 0xd8u)
        ok = TitanAssetImage_DecodeJpeg(source, source_size, TITAN_ASSET_KIND_APP_MEDIA,
            destination, options);
    else return 0;
    return ok;
}

int TitanAssetImage_ConvertToTimg(const unsigned char *source, unsigned int source_size,
    TitanAssetKind kind, unsigned char *destination, unsigned int destination_capacity,
    unsigned int *out_size)
{
    return TitanAssetImage_ConvertToTimgEx(source, source_size, kind, destination,
        destination_capacity, out_size, 0);
}
