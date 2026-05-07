// src/wgpu/shaders/vertex.wgsl
//
// Reproject a 16-bit depth raster into world-space points and emit them
// as point primitives. We use vertex_index as both the (u, v) index
// into the depth texture and the instance-less per-point identity --
// the draw call is `draw(width * height, 1, 0, 0)` so vid maps 1:1
// onto raster cells.

struct Uniforms {
    viewProj   : mat4x4<f32>,    // camera matrix from CPU
    width      : u32,            // depth width
    height     : u32,            // depth height
    bitDepth   : u32,            // 8, 10, 12; max value = (1 << bitDepth) - 1
    pointSize  : f32,            // pixel radius
    fxRcp      : f32,            // 1 / fx (intrinsics)
    fyRcp      : f32,            // 1 / fy
    cx         : f32,            // principal point x
    cy         : f32,            // principal point y
    depthScale : f32,            // multiply raw u16 by this to get metres
    near       : f32,            // cull plane (m); samples below -> discard
    far        : f32,            // cull plane (m)
};

@group(0) @binding(0) var<uniform> u   : Uniforms;
@group(0) @binding(1) var<storage, read> depth : array<u32>;
// We pack two uint16 samples per u32 to save bandwidth and side-step
// WebGPU's lack of u16 storage types.

struct VOut {
    @builtin(position) clip : vec4<f32>,
    @location(0)       worldZ : f32,
    @location(1)       valid  : f32,    // 1.0 = render, 0.0 = cull (discard in FS)
};

fn unpack_u16(packed : u32, low : bool) -> u32 {
    if (low) {
        return packed & 0xFFFFu;
    }
    return (packed >> 16u) & 0xFFFFu;
}

@vertex
fn vs_main(@builtin(vertex_index) vid : u32) -> VOut {
    var out : VOut;
    let total = u.width * u.height;
    if (vid >= total) {
        // Defensive: caller miscounted -- emit a degenerate point off-screen.
        out.clip   = vec4<f32>(2.0, 2.0, 2.0, 1.0);
        out.worldZ = 0.0;
        out.valid  = 0.0;
        return out;
    }

    let row    = vid / u.width;
    let col    = vid - row * u.width;
    let pIdx   = vid >> 1u;
    let isLow  = (vid & 1u) == 0u;
    let raw    = unpack_u16(depth[pIdx], isLow);

    // 0 is the depth-camera "no measurement" sentinel for every backend
    // we target (RealSense / Kinect / iToF). Cull these.
    if (raw == 0u) {
        out.clip   = vec4<f32>(2.0, 2.0, 2.0, 1.0);
        out.worldZ = 0.0;
        out.valid  = 0.0;
        return out;
    }

    let z = f32(raw) * u.depthScale;
    if (z < u.near || z > u.far) {
        out.clip   = vec4<f32>(2.0, 2.0, 2.0, 1.0);
        out.worldZ = 0.0;
        out.valid  = 0.0;
        return out;
    }

    // Pinhole back-projection.
    let x = (f32(col) - u.cx) * z * u.fxRcp;
    let y = (f32(row) - u.cy) * z * u.fyRcp;

    let world = vec4<f32>(x, y, z, 1.0);
    out.clip   = u.viewProj * world;
    out.worldZ = z;
    out.valid  = 1.0;
    return out;
}
