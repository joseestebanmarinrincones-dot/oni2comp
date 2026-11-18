#include "ps2_alpha_blend.hpp"

namespace ps2::gfx {
namespace {

using Op = Ps2AlphaOp;
using Factor = Ps2AlphaFactor;

Factor complement(Factor factor) {
    switch (factor) {
    case Factor::Zero: return Factor::One;
    case Factor::One: return Factor::Zero;
    case Factor::SrcColor: return Factor::OneMinusSrcColor;
    case Factor::OneMinusSrcColor: return Factor::SrcColor;
    case Factor::DstColor: return Factor::OneMinusDstColor;
    case Factor::OneMinusDstColor: return Factor::DstColor;
    case Factor::SrcAlpha: return Factor::OneMinusSrcAlpha;
    case Factor::OneMinusSrcAlpha: return Factor::SrcAlpha;
    case Factor::DstAlpha: return Factor::OneMinusDstAlpha;
    case Factor::OneMinusDstAlpha: return Factor::DstAlpha;
    case Factor::ConstantAlpha: return Factor::OneMinusConstantAlpha;
    case Factor::OneMinusConstantAlpha: return Factor::ConstantAlpha;
    }
    return Factor::One;
}

void set_alpha_blend(Ps2AlphaBlendState &state, Factor c) {
    state.rgb_op = Op::Add;
    state.rgb_src = c;
    state.rgb_dst = complement(c);
    state.alpha_op = Op::Add;
    state.alpha_src = Factor::One;
    state.alpha_dst = complement(c);
    state.name = c == Factor::One ? "replace" : "alpha";
    state.supported = true;
}

void set_additive(Ps2AlphaBlendState &state, Factor c) {
    state.rgb_op = Op::Add;
    state.rgb_src = c;
    state.rgb_dst = Factor::One;
    state.alpha_op = Op::Add;
    state.alpha_src = Factor::One;
    state.alpha_dst = Factor::One;
    state.name = c == Factor::One ? "additive-fixed" : "additive";
    state.supported = true;
}

void set_source(Ps2AlphaBlendState &state, Factor c) {
    state.rgb_op = Op::Add;
    state.rgb_src = c;
    state.rgb_dst = Factor::Zero;
    state.alpha_op = Op::Add;
    state.alpha_src = Factor::One;
    state.alpha_dst = Factor::Zero;
    state.name = "source";
    state.supported = true;
}

void set_destination_fade(Ps2AlphaBlendState &state, Factor c) {
    state.rgb_op = Op::Add;
    state.rgb_src = Factor::Zero;
    state.rgb_dst = complement(c);
    state.alpha_op = Op::Add;
    state.alpha_src = Factor::Zero;
    state.alpha_dst = complement(c);
    state.name = "destination-fade";
    state.supported = true;
}

void set_subtract(Ps2AlphaBlendState &state) {
    state.rgb_op = Op::Subtract;
    state.alpha_op = Op::Add;
    state.alpha_src = Factor::One;
    state.alpha_dst = Factor::Zero;
    state.name = "subtract";
    state.supported = true;
}

void set_replace(Ps2AlphaBlendState &state) {
    state.rgb_op = Op::Add;
    state.rgb_src = Factor::One;
    state.rgb_dst = Factor::Zero;
    state.alpha_op = Op::Add;
    state.alpha_src = Factor::One;
    state.alpha_dst = Factor::Zero;
    state.name = "replace";
    state.supported = true;
}

void set_noop(Ps2AlphaBlendState &state) {
    state.rgb_op = Op::Add;
    state.rgb_src = Factor::Zero;
    state.rgb_dst = Factor::One;
    state.alpha_op = Op::Add;
    state.alpha_src = Factor::Zero;
    state.alpha_dst = Factor::One;
    state.name = "noop";
    state.supported = true;
}

void set_zero(Ps2AlphaBlendState &state) {
    state.rgb_op = Op::Add;
    state.rgb_src = Factor::Zero;
    state.rgb_dst = Factor::Zero;
    state.alpha_op = Op::Add;
    state.alpha_src = Factor::Zero;
    state.alpha_dst = Factor::Zero;
    state.name = "zero";
    state.supported = true;
}

bool handle_d_only(Ps2AlphaBlendState &state, std::uint32_t d) {
    switch (d) {
    case 0: set_replace(state); return true;
    case 1: set_noop(state); return true;
    case 2: set_zero(state); return true;
    default: return false;
    }
}

}  // namespace

Ps2AlphaBlendState DecodePs2AlphaBlend(std::uint64_t alpha_reg) {
    Ps2AlphaBlendState state;
    const std::uint32_t a = static_cast<std::uint32_t>(alpha_reg & 0x3u);
    const std::uint32_t b =
        static_cast<std::uint32_t>((alpha_reg >> 2) & 0x3u);
    const std::uint32_t c =
        static_cast<std::uint32_t>((alpha_reg >> 4) & 0x3u);
    const std::uint32_t d =
        static_cast<std::uint32_t>((alpha_reg >> 6) & 0x3u);
    const std::uint32_t fix =
        static_cast<std::uint32_t>((alpha_reg >> 32) & 0xFFu);
    state.fix = static_cast<std::uint8_t>(fix);

    Factor factor = Factor::One;
    switch (c) {
    case 0: factor = Factor::SrcAlpha; break;
    case 1: factor = Factor::DstAlpha; break;
    case 2:
        factor = fix == 0x80u ? Factor::One : Factor::ConstantAlpha;
        break;
    default:
        state.name = "invalid-c";
        return state;
    }

    // A/B = 0: Cs, 1: Cd, 2: 0.   D = 0: Cs, 1: Cd, 2: 0.
    if (a == 0u && b == 1u) {          // (Cs - Cd)
        if (d == 1u) {
            set_alpha_blend(state, factor);
            return state;
        }
        if (d == 2u) {                 // (Cs - Cd) * C
            set_subtract(state);
            state.rgb_src = factor;
            state.rgb_dst = factor;
            return state;
        }
        state.name = "unsupported-d-cs";
        return state;
    }

    if (a == 0u && b == 2u) {          // Cs
        if (d == 1u) {
            set_additive(state, factor);
            return state;
        }
        if (d == 2u) {
            set_source(state, factor);
            return state;
        }
        state.name = "unsupported-d-cs";
        return state;
    }

    if (a == 2u && b == 1u) {          // -Cd
        if (d == 1u) {
            set_destination_fade(state, factor);
            return state;
        }
        if (d == 2u) {                 // -Cd * C
            set_subtract(state);
            state.rgb_src = Factor::Zero;
            state.rgb_dst = factor;
            state.name = "destination-subtract";
            return state;
        }
        if (d == 0u) {                 // Cs - Cd * C
            set_subtract(state);
            state.rgb_src = Factor::One;
            state.rgb_dst = factor;
            state.name = "subtract-source";
            return state;
        }
        state.name = "unsupported-d-cs";
        return state;
    }

    if (a == 2u && b == 2u) {          // 0
        if (handle_d_only(state, d)) return state;
        state.name = "invalid-d";
        return state;
    }

    if (a == 1u && b == 2u) {          // Cd
        if (d == 2u) {                 // Cd * C
            state.rgb_op = Op::Add;
            state.rgb_src = Factor::Zero;
            state.rgb_dst = factor;
            state.alpha_op = Op::Add;
            state.alpha_src = Factor::Zero;
            state.alpha_dst = factor;
            state.name = "destination-multiply";
            state.supported = true;
            return state;
        }
        if (d == 0u) {                 // Cs + Cd * C
            state.rgb_op = Op::Add;
            state.rgb_src = Factor::One;
            state.rgb_dst = factor;
            state.alpha_op = Op::Add;
            state.alpha_src = Factor::One;
            state.alpha_dst = factor;
            state.name = "add-destination";
            state.supported = true;
            return state;
        }
        state.name = "unsupported-d-cd";
        return state;
    }

    if (a == 1u && b == 1u) {          // 0
        if (handle_d_only(state, d)) return state;
        state.name = "invalid-d";
        return state;
    }

    if (a == 1u && b == 0u) {          // Cd - Cs
        if (d == 2u) {                 // (Cd - Cs) * C
            state.rgb_op = Op::ReverseSubtract;
            state.rgb_src = factor;
            state.rgb_dst = factor;
            state.alpha_op = Op::Add;
            state.alpha_src = Factor::One;
            state.alpha_dst = Factor::Zero;
            state.name = "reverse-subtract";
            state.supported = true;
            return state;
        }
        state.name = "unsupported-d-cs";
        return state;
    }

    if (a == 0u && b == 0u) {          // 0
        if (handle_d_only(state, d)) return state;
        state.name = "invalid-d";
        return state;
    }

    state.name = "unsupported";
    return state;
}

}  // namespace ps2::gfx
