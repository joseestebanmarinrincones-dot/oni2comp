const SHARPNESS: f32 = 0.14;
// Smooth edges: how slanted an edge must be (smaller direction part / larger one) and how far along it the blend may reach.
const EDGE_SLANT: f32 = 0.04;
const EDGE_REACH: f32 = 6.0;

struct Blit {
    width: u32,
    height: u32,
    current: u32,
    previous: u32,
    blend: u32,
    out_width: u32,
    out_height: u32,
    options: u32,
}

@group(0) @binding(0) var<storage, read> pixels: array<u32>;
@group(0) @binding(1) var<uniform> blit: Blit;

struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs(@builtin(vertex_index) i: u32) -> VertexOut {
    let p = vec2<f32>(f32((i << 1u) & 2u), f32(i & 2u));
    var o: VertexOut;
    o.position = vec4<f32>(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = vec2<f32>(p.x, 1.0 - p.y);
    return o;
}

fn texel(base: u32, x: i32, y: i32) -> vec4<f32> {
    let cx = u32(clamp(x, 0, i32(blit.width) - 1));
    let cy = u32(clamp(y, 0, i32(blit.height) - 1));
    return unpack4x8unorm(pixels[base + cy * blit.width + cx]);
}

// Three ways to enlarge the picture to the window (bit 2 below). Sharp (the default of bit 1 = 0): every PS2 pixel stays a hard square and only
// its border blends, the way an emulator shows it. Soft (bit 1): a plain smooth blend between neighbouring pixels everywhere,
// which is closest to what a TV did to the picture. Sharp (bit 2): see sharp_scene.
fn sample(base: u32, uv: vec2<f32>) -> vec4<f32> {
    let size = vec2<f32>(f32(blit.width), f32(blit.height));
    let t = uv * size;
    // Look 12 (fast): the nearest source pixel, one read, no blend. It is the cheapest enlargement.
    if ((blit.options & 64u) != 0u) { return texel(base, i32(floor(t.x)), i32(floor(t.y))); }
    var p: vec2<f32>;
    if ((blit.options & 6u) != 0u) {
        p = t - 0.5;
    } else {
        let scale = max(floor(vec2<f32>(f32(blit.out_width), f32(blit.out_height)) / size), vec2<f32>(1.0));
        let d = fract(t) - 0.5;
        let region = 0.5 - 0.5 / scale;
        p = floor(t) + (d - clamp(d, -region, region)) * scale;
    }
    let origin = floor(p);
    let f = p - origin;
    let x = i32(origin.x);
    let y = i32(origin.y);
    let top = mix(texel(base, x, y), texel(base, x + 1, y), f.x);
    let bottom = mix(texel(base, x, y + 1), texel(base, x + 1, y + 1), f.x);
    return mix(top, bottom, f.y);
}

fn scene(uv: vec2<f32>) -> vec3<f32> {
    var c = sample(blit.current, uv).rgb;
    if (blit.blend != 0u) { c = (c + sample(blit.previous, uv).rgb) * 0.5; }
    return c;
}

// Catmull-Rom weights (the cubic that passes through every source pixel and has a little overshoot at edges) for the four source
// pixels at offsets -1, 0, 1, 2 around a position with fraction f. Published in 1974; this is the textbook polynomial.
fn catmull_rom(f: f32) -> vec4<f32> {
    let f2 = f * f;
    let f3 = f2 * f;
    return vec4<f32>(-0.5 * f3 + f2 - 0.5 * f, 1.5 * f3 - 2.5 * f2 + 1.0, -1.5 * f3 + 2.0 * f2 + 0.5 * f, 0.5 * f3 - 0.5 * f2);
}

// Bilinear weights over the four source columns (or rows) -1, 0, 1, 2 around the pixel at floor(p), for a position u (in pixels
// from floor(p), between -1 and 2).
fn linear_weights(u: f32) -> vec4<f32> {
    let b = floor(u);
    let t = u - b;
    if (b < 0.0) { return vec4<f32>(1.0 - t, t, 0.0, 0.0); }
    if (b < 1.0) { return vec4<f32>(0.0, 1.0 - t, t, 0.0); }
    return vec4<f32>(0.0, 0.0, 1.0 - t, t);
}

fn row4(a: vec3<f32>, b: vec3<f32>, c: vec3<f32>, d: vec3<f32>, w: vec4<f32>) -> vec3<f32> {
    return a * w.x + b * w.y + c * w.z + d * w.w;
}

// Sharp look: a Catmull-Rom cubic enlargement (text and the health bar keep their edges, unlike a plain blend), then a light
// contrast-adaptive sharpening: the pixel is pushed away from the average of its four neighbours on the screen, less where the
// neighbourhood is already near black or white (so that edges do not get halos). Written from the public description of the idea
// (the amount follows how much room there is to the nearest of 0 and 1), not from any source code.
// The 4x4 source pixels around the position are read once (16 reads; the soft look reads 4) into named variables (an array that is
// indexed while running was slower on the Intel chip), and the cubic and the four neighbour blends are all taken from them.
fn sharp_scene(uv: vec2<f32>) -> vec3<f32> {
    let size = vec2<f32>(f32(blit.width), f32(blit.height));
    let p = uv * size - 0.5;
    let origin = floor(p);
    let f = p - origin;
    let x = i32(origin.x);
    let y = i32(origin.y);
    let t00 = scene_texel(x + -1, y + -1);
    let t01 = scene_texel(x + 0, y + -1);
    let t02 = scene_texel(x + 1, y + -1);
    let t03 = scene_texel(x + 2, y + -1);
    let t10 = scene_texel(x + -1, y + 0);
    let t11 = scene_texel(x + 0, y + 0);
    let t12 = scene_texel(x + 1, y + 0);
    let t13 = scene_texel(x + 2, y + 0);
    let t20 = scene_texel(x + -1, y + 1);
    let t21 = scene_texel(x + 0, y + 1);
    let t22 = scene_texel(x + 1, y + 1);
    let t23 = scene_texel(x + 2, y + 1);
    let t30 = scene_texel(x + -1, y + 2);
    let t31 = scene_texel(x + 0, y + 2);
    let t32 = scene_texel(x + 1, y + 2);
    let t33 = scene_texel(x + 2, y + 2);
    // one screen pixel in source pixels (at most one source pixel, so the neighbours stay inside the 4x4 block)
    let d = clamp(size / vec2<f32>(f32(blit.out_width), f32(blit.out_height)), vec2<f32>(0.0), vec2<f32>(0.99));
    let cx = catmull_rom(f.x);
    let cy = catmull_rom(f.y);
    let lx = linear_weights(f.x);
    let ly = linear_weights(f.y);
    let wxm = linear_weights(f.x - d.x);
    let wxp = linear_weights(f.x + d.x);
    let wyn = linear_weights(f.y - d.y);
    let wys = linear_weights(f.y + d.y);
    let c = row4(t00, t01, t02, t03, cx) * cy.x + row4(t10, t11, t12, t13, cx) * cy.y + row4(t20, t21, t22, t23, cx) * cy.z + row4(t30, t31, t32, t33, cx) * cy.w;
    let n = row4(t00, t01, t02, t03, lx) * wyn.x + row4(t10, t11, t12, t13, lx) * wyn.y + row4(t20, t21, t22, t23, lx) * wyn.z + row4(t30, t31, t32, t33, lx) * wyn.w;
    let s = row4(t00, t01, t02, t03, lx) * wys.x + row4(t10, t11, t12, t13, lx) * wys.y + row4(t20, t21, t22, t23, lx) * wys.z + row4(t30, t31, t32, t33, lx) * wys.w;
    let w = row4(t00, t01, t02, t03, wxm) * ly.x + row4(t10, t11, t12, t13, wxm) * ly.y + row4(t20, t21, t22, t23, wxm) * ly.z + row4(t30, t31, t32, t33, wxm) * ly.w;
    let e = row4(t00, t01, t02, t03, wxp) * ly.x + row4(t10, t11, t12, t13, wxp) * ly.y + row4(t20, t21, t22, t23, wxp) * ly.z + row4(t30, t31, t32, t33, wxp) * ly.w;
    let mn = min(min(min(n, s), min(w, e)), c);
    let mx = max(max(max(n, s), max(w, e)), c);
    let amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3<f32>(0.0001)), vec3<f32>(0.0), vec3<f32>(1.0)));
    let k = -amp * SHARPNESS;
    return (c + (n + s + w + e) * k) / (1.0 + 4.0 * k);
}

fn luma(c: vec3<f32>) -> f32 { return dot(c, vec3<f32>(0.299, 0.587, 0.114)); }

// The colour of one source pixel (no blending between pixels): the cheap read the edge test uses.
fn scene_texel(x: i32, y: i32) -> vec3<f32> {
    var c = texel(blit.current, x, y).rgb;
    if (blit.blend != 0u) { c = (c + texel(blit.previous, x, y).rgb) * 0.5; }
    return c;
}

fn edge_aa(uv: vec2<f32>, center: vec3<f32>) -> vec3<f32> {
    // Fast approximate edge AA, evaluated in image space. Flat regions return
    // unchanged, while edge direction controls subpixel reconstruction.
    // The edge test reads the five source pixels around this one (one read each, instead of a smooth
    // blend per neighbour): most of the picture is flat and stops here, so this is what the picture costs.
    let texel_step = 1.0 / vec2<f32>(f32(blit.width), f32(blit.height));
    let c = vec2<i32>(floor(uv * vec2<f32>(f32(blit.width), f32(blit.height))));
    let nw = luma(scene_texel(c.x - 1, c.y - 1));
    let ne = luma(scene_texel(c.x + 1, c.y - 1));
    let sw = luma(scene_texel(c.x - 1, c.y + 1));
    let se = luma(scene_texel(c.x + 1, c.y + 1));
    let m = luma(scene_texel(c.x, c.y));
    let low = min(m, min(min(nw, ne), min(sw, se)));
    let high = max(m, max(max(nw, ne), max(sw, se)));
    if (high - low < max(0.0312, high * 0.125)) { return center; }
    var direction = vec2<f32>(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    let reduce = max((nw + ne + sw + se) * 0.03125, 0.0078125);
    // The dotted noise the first version left along hard edges (health bar ends, text) came from here: an edge that is nearly
    // straight up/down or left/right got a direction of up to 8 source pixels, so the blend reached far along the edge and pulled in
    // unrelated dark and bright pixels as dots and ticks. A straight edge has no stairs to smooth, so it is left alone, and the
    // reach along a slanted one is at most EDGE_REACH source pixels.
    let ax = abs(direction.x);
    let ay = abs(direction.y);
    if (min(ax, ay) < max(max(ax, ay) * EDGE_SLANT, reduce)) { return center; }
    direction = clamp(direction / (min(ax, ay) + reduce), vec2<f32>(-EDGE_REACH), vec2<f32>(EDGE_REACH)) * texel_step;
    // The blends along the edge are taken from the plain smooth blend (4 reads each; the cubic would cost 16 each), and what they change
    // against the plain blend at this pixel is added to `center`. For the soft and crisp looks `center` is that blend itself, so the
    // result is the blend along the edge; for Sharp the cubic and the sharpening stay (text stays crisp) and the stairs are smoothed on top.
    let plain = scene(uv);
    let a = 0.5 * (scene(uv - direction / 6.0) + scene(uv + direction / 6.0));
    let b = a * 0.5 + 0.25 * (scene(uv - direction * 0.5) + scene(uv + direction * 0.5));
    let lb = luma(b);
    return center + (select(b, a, lb < low || lb > high) - plain);
}

// FSR 1 (AMD FidelityFX Super Resolution 1.0, MIT License, Copyright (c) 2021 Advanced Micro Devices, Inc.): EASU, the edge adaptive
// upscaler, and RCAS, the contrast adaptive sharpener that runs after it. Ported from ffx_fsr1.h (its 32-bit scalar paths); the texture
// reads are this shader's source pixels (scene_texel). EASU limits its result to the min and max of the 4 nearest source pixels, so it adds no ringing.
const FSR_RCAS_LIMIT: f32 = 0.1875;  // 0.25 - 1/16 in the reference: the most RCAS may take
// The speed of the game and the picture settings arrive in blit.options: bits 8..15 hold FSR 1 sharpness (0..100 percent) and
// bits 16..23 hold FXAA strength. 50 percent keeps the sharpness this shader had before (0.2 stops of RCAS), 75 percent the old
// FXAA amount. Both are adjustable in the quick menu (F1) and keys.ini.
fn fsr_sharpness_stops() -> f32 { return 0.4 * (1.0 - f32((blit.options >> 8u) & 255u) / 100.0); }
fn fxaa_strength() -> f32 { return f32((blit.options >> 16u) & 255u) / 100.0; }

fn luma2(c: vec3<f32>) -> f32 { return 0.5 * c.r + c.g + 0.5 * c.b; }  // twice the luma, as the reference computes it

// One EASU tap: a negative-lobe window over the distance, rotated to the edge direction and stretched by len.
fn fsr_easu_tap(off: vec2<f32>, dir: vec2<f32>, len: vec2<f32>, lob: f32, clp: f32, c: vec3<f32>) -> vec4<f32> {
    let v = vec2<f32>(off.x * dir.x + off.y * dir.y, off.x * (-dir.y) + off.y * dir.x) * len;
    let d2 = min(v.x * v.x + v.y * v.y, clp);
    var wb = (2.0 / 5.0) * d2 - 1.0;
    let wa = lob * d2 - 1.0;
    wb = (25.0 / 16.0) * (wb * wb) - (25.0 / 16.0 - 1.0);
    let w = wb * wa * wa;
    return vec4<f32>(c * w, w);
}

// The gradient and length one bilinear cell (weight w) adds: x and y gradients weighted by w, and the edge length term.
fn fsr_easu_set(w: f32, la: f32, lb: f32, lc: f32, ld: f32, le: f32) -> vec3<f32> {
    let sx = clamp(abs(ld - lb) / max(max(abs(ld - lc), abs(lc - lb)), 1e-6), 0.0, 1.0);
    let sy = clamp(abs(le - la) / max(max(abs(le - lc), abs(lc - la)), 1e-6), 0.0, 1.0);
    return vec3<f32>((ld - lb) * w, (le - la) * w, (sx * sx + sy * sy) * w);
}

// EASU for the output pixel ip (integer position in the picture, before scaling), from the 12 nearest source pixels.
fn fsr_easu(ip: vec2<f32>) -> vec3<f32> {
    let size = vec2<f32>(f32(blit.width), f32(blit.height));
    let scale = size / vec2<f32>(f32(blit.out_width), f32(blit.out_height));
    let p = (ip + 0.5) * scale - 0.5;
    let fp = floor(p);
    let pp = p - fp;
    let x = i32(fp.x);
    let y = i32(fp.y);
    // the 12 taps, named as in the reference: b c / e f g h / i j k l / n o
    let b = scene_texel(x, y - 1);
    let c = scene_texel(x + 1, y - 1);
    let e = scene_texel(x - 1, y);
    let f = scene_texel(x, y);
    let g = scene_texel(x + 1, y);
    let h = scene_texel(x + 2, y);
    let i = scene_texel(x - 1, y + 1);
    let j = scene_texel(x, y + 1);
    let k = scene_texel(x + 1, y + 1);
    let l = scene_texel(x + 2, y + 1);
    let n = scene_texel(x, y + 2);
    let o = scene_texel(x + 1, y + 2);
    let bL = luma2(b); let cL = luma2(c); let eL = luma2(e); let fL = luma2(f);
    let gL = luma2(g); let hL = luma2(h); let iL = luma2(i); let jL = luma2(j);
    let kL = luma2(k); let lL = luma2(l); let nL = luma2(n); let oL = luma2(o);
    let wS = (1.0 - pp.x) * (1.0 - pp.y);
    let wT = pp.x * (1.0 - pp.y);
    let wU = (1.0 - pp.x) * pp.y;
    let wV = pp.x * pp.y;
    let acc0 = fsr_easu_set(wS, bL, eL, fL, gL, jL) + fsr_easu_set(wT, cL, fL, gL, hL, kL)
             + fsr_easu_set(wU, fL, iL, jL, kL, nL) + fsr_easu_set(wV, gL, jL, kL, lL, oL);
    var dir = acc0.xy;
    var len = acc0.z;
    let dir2 = dir * dir;
    var dirR = dir2.x + dir2.y;
    let zro = dirR < 1.0 / 32768.0;
    dirR = select(inverseSqrt(max(dirR, 1e-12)), 1.0, zro);
    dir.x = select(dir.x, 1.0, zro);
    dir = dir * dirR;
    len = len * 0.5;
    len = len * len;
    let stretch = (dir.x * dir.x + dir.y * dir.y) / max(max(abs(dir.x), abs(dir.y)), 1e-6);
    let len2 = vec2<f32>(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    let lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    let clp = 1.0 / lob;
    let mn4 = min(min(f, g), min(j, k));
    let mx4 = max(max(f, g), max(j, k));
    var acc = vec4<f32>(0.0);
    acc += fsr_easu_tap(vec2<f32>(0.0, -1.0) - pp, dir, len2, lob, clp, b);
    acc += fsr_easu_tap(vec2<f32>(1.0, -1.0) - pp, dir, len2, lob, clp, c);
    acc += fsr_easu_tap(vec2<f32>(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
    acc += fsr_easu_tap(vec2<f32>(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    acc += fsr_easu_tap(vec2<f32>(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    acc += fsr_easu_tap(vec2<f32>(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
    acc += fsr_easu_tap(vec2<f32>(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    acc += fsr_easu_tap(vec2<f32>(2.0, 1.0) - pp, dir, len2, lob, clp, l);
    acc += fsr_easu_tap(vec2<f32>(2.0, 0.0) - pp, dir, len2, lob, clp, h);
    acc += fsr_easu_tap(vec2<f32>(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    acc += fsr_easu_tap(vec2<f32>(1.0, 2.0) - pp, dir, len2, lob, clp, o);
    acc += fsr_easu_tap(vec2<f32>(0.0, 2.0) - pp, dir, len2, lob, clp, n);
    let avg = acc.rgb / max(acc.a, 1e-6);
    return min(mx4, max(mn4, avg));
}

// RCAS on the EASU output: a cross of 5 pixels, the negative lobe solved so that nothing clips out of 0 to 1.
fn fsr_rcas(ip: vec2<f32>) -> vec3<f32> {
    let b = fsr_easu(ip + vec2<f32>(0.0, -1.0));
    let d = fsr_easu(ip + vec2<f32>(-1.0, 0.0));
    let e = fsr_easu(ip);
    let f = fsr_easu(ip + vec2<f32>(1.0, 0.0));
    let h = fsr_easu(ip + vec2<f32>(0.0, 1.0));
    let mn4 = min(min(b, d), min(f, h));
    let mx4 = max(max(b, d), max(f, h));
    let hit_min = min(mn4, e) / max(4.0 * mx4, vec3<f32>(1e-6));
    let hit_max = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, vec3<f32>(-1e-6));
    let lobe_rgb = max(-hit_min, hit_max);
    let sharp = exp2(-fsr_sharpness_stops());
    let lobe = max(-FSR_RCAS_LIMIT, min(max(lobe_rgb.r, max(lobe_rgb.g, lobe_rgb.b)), 0.0)) * sharp;
    return (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
}

fn fsr(ip: vec2<f32>) -> vec3<f32> {
    var c: vec3<f32>;
    if ((blit.options & 16u) != 0u) { c = fsr_rcas(ip); } else { c = fsr_easu(ip); }
    return clamp(c, vec3<f32>(0.0), vec3<f32>(1.0));
}

// Anti-aliasing (bit 5): where the picture has a strong edge (the luma of the 4 neighbours differs a lot), the pixel is blended
// with the two neighbours that lie across that edge. Written for this shader from the general idea of edge blending; it is not
// copied from any FXAA source.
fn base_color(uv: vec2<f32>) -> vec3<f32> {
    if ((blit.options & 4u) != 0u) { return sharp_scene(uv); }
    return scene(uv);
}

fn fxaa_apply(c: vec3<f32>, cn: vec3<f32>, cs: vec3<f32>, ce: vec3<f32>, cw: vec3<f32>) -> vec3<f32> {
    let lc = luma(c);
    let ln = luma(cn);
    let ls = luma(cs);
    let le = luma(ce);
    let lw = luma(cw);
    let lmin = min(lc, min(min(ln, ls), min(le, lw)));
    let lmax = max(lc, max(max(ln, ls), max(le, lw)));
    let range = lmax - lmin;
    if (range < max(0.0312, lmax * 0.125)) { return c; }
    let vertical = abs(le + lw - 2.0 * lc) >= abs(ln + ls - 2.0 * lc);
    let avg = select(0.5 * (cn + cs), 0.5 * (ce + cw), vertical);
    let amount = clamp(range / max(lmax, 0.05) * fxaa_strength(), 0.0, 0.5);
    return mix(c, avg, amount);
}

// FXAA for the sharp and soft looks: its neighbours are made the same way as the centre, so text and the health bar keep their
// edges. The FSR look below uses fxaa_scene instead, whose neighbours are one bilinear read (FSR on five pixels would be much more
// work than the rest of the picture).
fn fxaa(uv: vec2<f32>, c: vec3<f32>) -> vec3<f32> {
    let px = 1.0 / vec2<f32>(f32(blit.out_width), f32(blit.out_height));
    return fxaa_apply(c, base_color(uv + vec2<f32>(0.0, -px.y)), base_color(uv + vec2<f32>(0.0, px.y)),
                     base_color(uv + vec2<f32>(px.x, 0.0)), base_color(uv + vec2<f32>(-px.x, 0.0)));
}

fn fxaa_scene(uv: vec2<f32>, c: vec3<f32>) -> vec3<f32> {
    let px = 1.0 / vec2<f32>(f32(blit.out_width), f32(blit.out_height));
    return fxaa_apply(c, scene(uv + vec2<f32>(0.0, -px.y)), scene(uv + vec2<f32>(0.0, px.y)),
                     scene(uv + vec2<f32>(px.x, 0.0)), scene(uv + vec2<f32>(-px.x, 0.0)));
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4<f32> {
    if ((blit.options & 8u) != 0u) {
        var c = fsr(floor(in.uv * vec2<f32>(f32(blit.out_width), f32(blit.out_height))));
        if ((blit.options & 32u) != 0u) { c = fxaa_scene(in.uv, c); }
        return vec4<f32>(clamp(c, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
    }
    var c: vec3<f32>;
    if ((blit.options & 4u) != 0u) {
        c = sharp_scene(in.uv);
    } else {
        c = scene(in.uv);
    }
    if ((blit.options & 1u) != 0u) { c = edge_aa(in.uv, c); }
    if ((blit.options & 32u) != 0u) { c = fxaa(in.uv, c); }
    return vec4<f32>(clamp(c, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
}
