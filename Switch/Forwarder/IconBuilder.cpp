#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "Switch/Forwarder/IconBuilder.h"

#include "Common/Data/Format/PNGLoad.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Log.h"
#include "ext/jpge/jpge.h"
#include "ext/jpge/jpgd.h"

namespace Forwarder {

namespace {

constexpr int kIconSize = 256;
constexpr int kTextAreaHeight = 96;              // Bottom strip reserved for the "PSP" wordmark.
constexpr int kIconAreaHeight = kIconSize - kTextAreaHeight;

// Catmull-Rom cubic convolution kernel (Keys, a = -0.5) - a standard,
// unfancy bicubic weighting, not a specialized/sharpened variant.
float CubicWeight(float x) {
    constexpr float a = -0.5f;
    x = std::fabs(x);
    if (x <= 1.0f) {
        return (a + 2.0f) * x * x * x - (a + 3.0f) * x * x + 1.0f;
    } else if (x < 2.0f) {
        return a * x * x * x - 5.0f * a * x * x + 8.0f * a * x - 4.0f * a;
    }
    return 0.0f;
}

uint8_t SampleChannelBicubic(const uint8_t *src, int srcW, int srcH, int channel, float sx, float sy) {
    int ix = (int)std::floor(sx);
    int iy = (int)std::floor(sy);
    float fx = sx - ix;
    float fy = sy - iy;

    float result = 0.0f;
    for (int m = -1; m <= 2; m++) {
        int yy = std::clamp(iy + m, 0, srcH - 1);
        float rowSum = 0.0f;
        for (int n = -1; n <= 2; n++) {
            int xx = std::clamp(ix + n, 0, srcW - 1);
            rowSum += CubicWeight(n - fx) * src[(yy * srcW + xx) * 4 + channel];
        }
        result += CubicWeight(m - fy) * rowSum;
    }
    return (uint8_t)std::clamp(result + 0.5f, 0.0f, 255.0f);
}

// Bicubic scale-blit of an RGBA source rect into an RGB destination rect at
// (dstX, dstY), size (dstW, dstH). Alpha is ignored (matches source PSP/PPSSPP
// icons, which are opaque) - only RGB is resampled and copied.
void BlitRgbaToRgbBicubic(const uint8_t *src, int srcW, int srcH,
                           uint8_t *dst, int dstStride,
                           int dstX, int dstY, int dstW, int dstH) {
    for (int y = 0; y < dstH; y++) {
        float sy = srcH > 0 ? ((y + 0.5f) * srcH) / dstH - 0.5f : 0.0f;
        for (int x = 0; x < dstW; x++) {
            float sx = srcW > 0 ? ((x + 0.5f) * srcW) / dstW - 0.5f : 0.0f;
            uint8_t *d = dst + ((dstY + y) * dstStride + (dstX + x)) * 3;
            d[0] = SampleChannelBicubic(src, srcW, srcH, 0, sx, sy);
            d[1] = SampleChannelBicubic(src, srcW, srcH, 1, sx, sy);
            d[2] = SampleChannelBicubic(src, srcW, srcH, 2, sx, sy);
        }
    }
}

// Loads and decodes assets/forwarder_background.jpg (PPSSPP's own app icon,
// blurred/dimmed, with the "PSP" wordmark pre-composited on top - see
// gen_composite_background.py) via the normal VFS asset path, same as fonts
// and other bundled resources, rather than baking it into the binary as a
// byte array. Returns an all-black kIconSize x kIconSize buffer on failure
// (missing/corrupt asset) so icon generation still produces something usable
// instead of aborting the whole forwarder-install flow.
std::vector<uint8_t> LoadBackgroundRgb() {
    std::vector<uint8_t> rgb(kIconSize * kIconSize * 3, 0);

    size_t jpegSize = 0;
    uint8_t *jpegData = g_VFS.ReadFile("forwarder_background.jpg", &jpegSize);
    if (!jpegData) {
        ERROR_LOG(Log::Forwarder, "forwarder_background.jpg not found in assets, using black background");
        return rgb;
    }

    int width = 0, height = 0, actualComps = 0;
    unsigned char *decoded = jpgd::decompress_jpeg_image_from_memory(
        jpegData, (int)jpegSize, &width, &height, &actualComps, 3);
    delete[] jpegData;

    if (!decoded) {
        ERROR_LOG(Log::Forwarder, "failed to decode forwarder_background.jpg, using black background");
        return rgb;
    }
    if (width != kIconSize || height != kIconSize) {
        ERROR_LOG(Log::Forwarder, "forwarder_background.jpg is %dx%d, expected %dx%d, using black background", width, height, kIconSize, kIconSize);
        free(decoded);
        return rgb;
    }

    memcpy(rgb.data(), decoded, rgb.size());
    free(decoded);
    return rgb;
}

std::vector<uint8_t> EncodeRgb256AsJpeg(const std::vector<uint8_t> &rgb) {
    std::vector<uint8_t> jpegBuf(128 * 1024);
    int bufSize = (int)jpegBuf.size();
    jpge::params params;
    params.m_quality = 90;
    // H1V1 (4:4:4, no chroma subsampling) - confirmed via byte-level
    // inspection that a known-working reference forwarder's Control NCA icon
    // uses no subsampling; H2V2 (4:2:0, this encoder's default) produces a
    // technically-valid but differently-encoded baseline JPEG that the
    // Switch Home Menu's icon decoder does not render correctly (shows as a
    // corrupted tile).
    params.m_subsampling = jpge::H1V1;

    if (!jpge::compress_image_to_jpeg_file_in_memory(jpegBuf.data(), bufSize, kIconSize, kIconSize, 3, rgb.data(), params)) {
        ERROR_LOG(Log::Forwarder, "JPEG encode failed for forwarder icon");
        return {};
    }
    jpegBuf.resize(bufSize);
    return jpegBuf;
}

}  // namespace

std::vector<uint8_t> BuildForwarderIcon(const uint8_t *pngData, size_t pngSize, bool isPspGameIcon) {
    int width = 0, height = 0;
    unsigned char *decoded = nullptr;
    if (!pngLoadPtr(pngData, pngSize, &width, &height, &decoded) || !decoded) {
        ERROR_LOG(Log::Forwarder, "failed to decode source icon PNG, pngSize=%zu isPspGameIcon=%d", pngSize, isPspGameIcon ? 1 : 0);
        return {};
    }

    std::vector<uint8_t> rgb;

    if (isPspGameIcon) {
        // Start from the background asset (PPSSPP's blurred/dimmed app icon
        // with the "PSP" wordmark pre-composited on top) rather than solid
        // black, then letterbox the actual per-game icon over it - the
        // wordmark's baked position assumes a standard 144x80 PSP ICON0.PNG
        // at this same 1.5x/letterbox math, which matches virtually every
        // real game icon (see gen_composite_background.py).
        rgb = LoadBackgroundRgb();

        // Fixed 1.5x pixel scale (not a stretch-to-fill), centered in the
        // icon area, letterboxed with background showing through on
        // whichever sides don't fill - stretching a PSP icon (typically well
        // under 256px wide) to fill the full canvas width made it look
        // oversized and blocky.
        constexpr float kGameIconScale = 1.5f;
        int scaledW = (int)(width * kGameIconScale + 0.5f);
        int scaledH = (int)(height * kGameIconScale + 0.5f);
        // Clamp to the icon area if 1.5x would overflow it, preserving
        // aspect ratio rather than distorting.
        if (scaledW > kIconSize || scaledH > kIconAreaHeight) {
            float clamp = std::min((float)kIconSize / scaledW, (float)kIconAreaHeight / scaledH);
            scaledW = (int)(scaledW * clamp);
            scaledH = (int)(scaledH * clamp);
        }
        int xOff = (kIconSize - scaledW) / 2;
        int yOff = (kIconAreaHeight - scaledH) / 2;
        BlitRgbaToRgbBicubic(decoded, width, height, rgb.data(), kIconSize, xOff, yOff, scaledW, scaledH);
    } else {
        // Already-square source (PPSSPP's own icon) - fill the whole tile.
        rgb.resize(kIconSize * kIconSize * 3);
        BlitRgbaToRgbBicubic(decoded, width, height, rgb.data(), kIconSize, 0, 0, kIconSize, kIconSize);
    }

    free(decoded);
    std::vector<uint8_t> jpeg = EncodeRgb256AsJpeg(rgb);
    return jpeg;
}

}  // namespace Forwarder

#endif  // PPSSPP_PLATFORM(SWITCH)
