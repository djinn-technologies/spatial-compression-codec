// src/wgpu/shaders/fragment.wgsl
//
// Per-point colourisation using a turbo-like ramp keyed on world-space
// Z. Cheap and avoids per-vertex colour traffic.

struct FIn {
    @location(0) worldZ : f32,
    @location(1) valid  : f32,
};

// 5-stop turbo-like gradient (cheap; dependency-free).
fn ramp(t : f32) -> vec3<f32> {
    let s0 = vec3<f32>(0.18, 0.07, 0.31);    // deep purple (near)
    let s1 = vec3<f32>(0.13, 0.41, 0.69);    // brand blue
    let s2 = vec3<f32>(0.16, 0.74, 0.74);    // teal
    let s3 = vec3<f32>(0.95, 0.79, 0.18);    // amber
    let s4 = vec3<f32>(0.85, 0.16, 0.13);    // red (far)

    let tc = clamp(t, 0.0, 1.0);
    if (tc < 0.25) { return mix(s0, s1, tc * 4.0); }
    if (tc < 0.5)  { return mix(s1, s2, (tc - 0.25) * 4.0); }
    if (tc < 0.75) { return mix(s2, s3, (tc - 0.5)  * 4.0); }
    return mix(s3, s4, (tc - 0.75) * 4.0);
}

@fragment
fn fs_main(in : FIn) -> @location(0) vec4<f32> {
    if (in.valid < 0.5) {
        discard;
    }
    // Map z in [0.2, 6.0] m to [0, 1].
    let t = (in.worldZ - 0.2) / 5.8;
    return vec4<f32>(ramp(t), 1.0);
}
