/**
 * This kernel converts NV12 (Y plane followed by interleaved UV plane) to 
 * packed RGB (3 bytes per pixel). It is optimized for Intel GPUs and other 
 * OpenCL devices by using vectorized math and efficient memory storage.
 * 
 * Color Space: BT.601 Limited Range (Standard for SD/HD video)
 * Formula:
 *   Y' = 1.164 * (Y - 16)
 *   R  = Y' + 1.596 * (V - 128)
 *   G  = Y' - 0.391 * (U - 128) - 0.813 * (V - 128)
 *   B  = Y' + 2.018 * (U - 128)
 */

// BT.601 Limited Range Constants
#define K_Y      1.164383f
#define K_RV     1.596027f
#define K_GU     0.391762f
#define K_GV     0.812968f
#define K_BU     2.017232f

#define Y_OFF    16.0f
#define UV_OFF   128.0f

__kernel void nv12_to_rgb(__global const uchar *restrict nv12,
                          __global uchar *restrict rgb, const uint width,
                          const uint height) {
  // We process 1 pixel per thread for maximum compatibility with existing host
  // code,
  // but use optimized OpenCL features for performance.
    const int x = get_global_id(0);
    const int y = get_global_id(1);

    // Bounds check
    if (x >= width || y >= height) {
        return;
    }

    // --- 1. Memory Access ---
    
    // Luma (Y) is full resolution at the start of the buffer
    const uint y_idx = y * width + x;
    float Y = (float)nv12[y_idx];

    // Chroma (UV) is half resolution (2x2 subsampling)
    // NV12 Layout: [Y plane (W*H)][UV plane (W*H/2)]
    // UV plane stores U, V interleaved: U0, V0, U1, V1, ...
    const uint uv_base = width * height;
    const uint uv_row  = y >> 1;
    const uint uv_col  = x & ~1; // Round down to even for the U/V pair
    const uint uv_idx  = uv_base + (uv_row * width) + uv_col;

    // Load U and V
    float U = (float)nv12[uv_idx];
    float V = (float)nv12[uv_idx + 1];

    // --- 2. Color Conversion ---
    
    // Normalize and scale
    float Y_p = K_Y * (Y - Y_OFF);
    float U_p = U - UV_OFF;
    float V_p = V - UV_OFF;

    float3 rgb_f;
    rgb_f.x = Y_p + K_RV * V_p;
    rgb_f.y = Y_p - K_GU * U_p - K_GV * V_p;
    rgb_f.z = Y_p + K_BU * U_p;

    // --- 3. Storage ---
    
    // convert_uchar3_sat_rte:
    // - _sat: Clamps to [0, 255]
    // - _rte: Rounds to Nearest Even (higher quality than truncation)
    // vstore3: Optimized 24-bit write (p + offset * 3)
    const uint pixel_idx = y * width + x;
    vstore3(convert_uchar3_sat_rte(rgb_f), pixel_idx, rgb);
}