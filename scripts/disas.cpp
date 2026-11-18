// disas: a listing generator for the PlayStation 2 EE core (R5900).
//
// It reuses the recompiler's decoder (recomp/r5900.cpp), so the MMI and COP2
// instructions the game uses are named the same way the recompiler names them,
// and the function search (recomp/analysis.cpp), so the labels match the entry
// points the recompiler would translate.
//
//   disas <elf> [--entries entries.txt] [--entry HEX]... [--out FILE]
//   disas <raw.bin> --base HEX [--entry HEX]... [--out FILE]
//
// Options:
//   --out FILE       listing path ("-" or omitted: standard output)
//   --start HEX      first address to list (default: program base)
//   --end HEX        address after the last one to list (default: program end)
//   --entries FILE   entries.txt: bare addresses are used for an ELF; for a raw
//                    image the lines "BASE_CRC:OFFSET" whose base matches are used
//   --entry HEX      extra entry point, repeatable
//   --stats FILE     write a summary
//   --funcs FILE     write the label list (address, kind, name)
//   --all            also treat the image entry as a function start (ELF default)
//   --quiet          do not print the summary to stderr
//
// The output is a GNU-as style listing, not something that assembles back: the
// point is to read the game's code and its function boundaries.

#include "analysis.hpp"
#include "elf.hpp"
#include "r5900.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace recomp;
using enum recomp::Op;

namespace {

const char *const kGpr[32] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
};

std::string gpr(unsigned r) { return std::string("$") + kGpr[r & 31u]; }

std::string lower(std::string text) {
    for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string hex(unsigned value, int width = 0) {
    char text[32];
    if (width) std::snprintf(text, sizeof text, "0x%0*x", width, value);
    else std::snprintf(text, sizeof text, "0x%x", value);
    return text;
}

std::string addr8(unsigned value) {
    char text[16];
    std::snprintf(text, sizeof text, "%08x", value);
    return text;
}

std::string simm(std::int32_t value) {
    char text[32];
    if (value < 0) std::snprintf(text, sizeof text, "-0x%x", static_cast<unsigned>(-value));
    else std::snprintf(text, sizeof text, "0x%x", static_cast<unsigned>(value));
    return text;
}

std::string signed_dec(std::int32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, "%d", value);
    return text;
}

// The names the recompiler uses are CamelCase; the listing wants the assembler's
// lower case spelling, plus the dotted forms of the FPU and the MMI suffixes.
struct Mnemonics {
    std::array<std::string, static_cast<std::size_t>(Op::Count)> names;

    Mnemonics() {
        for (std::size_t i = 0; i < names.size(); ++i) names[i] = lower(recomp::name(static_cast<Op>(i)));
        auto set = [&](Op op, const char *text) { names[static_cast<std::size_t>(op)] = text; };
        set(AddS, "add.s"); set(SubS, "sub.s"); set(MulS, "mul.s"); set(DivS, "div.s");
        set(SqrtS, "sqrt.s"); set(AbsS, "abs.s"); set(MovS, "mov.s"); set(NegS, "neg.s");
        set(RsqrtS, "rsqrt.s"); set(AddaS, "adda.s"); set(SubaS, "suba.s"); set(MulaS, "mula.s");
        set(MaddS, "madd.s"); set(MsubS, "msub.s"); set(MaddaS, "madda.s"); set(MsubaS, "msuba.s");
        set(CvtWS, "cvt.w.s"); set(CvtSW, "cvt.s.w"); set(MaxS, "max.s"); set(MinS, "min.s");
        set(CF, "c.f.s"); set(CEq, "c.eq.s"); set(CLt, "c.lt.s"); set(CLe, "c.le.s");
        set(Mfc0, "mfc0"); set(Mtc0, "mtc0"); set(Tlbr, "tlbr"); set(Tlbwi, "tlbwi");
        set(Tlbwr, "tlbwr"); set(Tlbp, "tlbp"); set(Eret, "eret"); set(Ei, "ei"); set(Di, "di");
        set(Mtsab, "mtsab"); set(Mtsah, "mtsah");
        set(Lqc2, "lqc2"); set(Sqc2, "sqc2"); set(Qmfc2, "qmfc2"); set(Cfc2, "cfc2");
        set(Qmtc2, "qmtc2"); set(Ctc2, "ctc2");
        set(Lwu, "lwu"); set(Ldl, "ldl"); set(Ldr, "ldr"); set(Sdl, "sdl"); set(Sdr, "sdr");
        set(Pmfhl, "pmfhl"); set(Pmthl, "pmthl");
    }
};

const Mnemonics &mnemonics() {
    static const Mnemonics table;
    return table;
}

const char *const kLane = "xyzw";

char lane_letter(unsigned index) { return index < 4 ? kLane[index] : '?'; }

std::string mask_suffix(unsigned mask) {
    std::string text = ".";
    for (unsigned i = 0; i < 4; ++i)
        if (mask & (8u >> i)) text += lane_letter(i);
    return text == "." ? std::string() : text;
}

// The VU macro instructions and the VU microcode share their upper-op encoding;
// the destination mask is the four bits at 24..21, the register is fd, and the
// broadcast lane of a `bc` instruction comes from bits 1..0.
struct VfNames {
    static std::string vf(unsigned index) { return "$vf" + std::to_string(index); }
    static std::string vi(unsigned index) { return "$vi" + std::to_string(index); }
};

std::string vu_base(Op op) {
    std::string name = lower(recomp::name(op));
    auto strip = [&](const char *suffix) {
        const std::size_t n = std::strlen(suffix);
        if (name.size() >= n && name.compare(name.size() - n, n, suffix) == 0) name.resize(name.size() - n);
    };
    switch (op) {
    case Vaddbc: case Vsubbc: case Vmaddbc: case Vmsubbc: case Vmaxbc: case Vminibc: case Vmulbc:
        strip("bc");
        break;
    case Vaddabc: case Vsubabc: case Vmaddabc: case Vmsubabc: case Vmulabc:
        strip("abc");
        break;
    case Vmulq: case Vmaxi: case Vmuli: case Vminii:
        break;
    case Vaddq: case Vmaddq: case Vaddi: case Vmaddi: case Vsubq: case Vmsubq: case Vsubi: case Vmsubi:
        break;
    default:
        break;
    }
    return name;
}

// Formats the COP2 (VU macro mode) instructions. The encoding is the same one
// the VU uses for its upper instructions, so a listing of the macro form is
// also the closest thing to a listing of the microcode the EE hands to VU1.
std::string vu_macro(const Insn &i) {
    const unsigned mask = i.dest(), fd = i.fd(), fs = i.fs(), ft = i.ft(), bc = i.bc() & 3u;
    const unsigned fsf = (i.word >> 21) & 3u, ftf = (i.word >> 23) & 3u;
    const std::string suffix = mask_suffix(mask);
    auto V = [](unsigned r) { return VfNames::vf(r); };
    auto I = [](unsigned r) { return VfNames::vi(r & 15u); };
    std::string name = vu_base(i.op);
    auto bc_name = [&] { name += lane_letter(bc); };

    switch (i.op) {
    case Vaddbc: case Vsubbc: case Vmaddbc: case Vmsubbc: case Vmaxbc: case Vminibc: case Vmulbc:
        bc_name();
        return name + suffix + " " + V(fd) + ", " + V(fs) + ", " + V(ft);
    case Vaddabc: case Vsubabc: case Vmaddabc: case Vmsubabc: case Vmulabc:
        bc_name();
        return name + suffix + " " + V(fs) + ", " + V(ft);
    case Vaddq: case Vmaddq: case Vsubq: case Vmsubq:
        return name + suffix + " " + V(fd) + ", " + V(fs) + ", $q";
    case Vaddi: case Vmaddi: case Vsubi: case Vmsubi:
        return name + suffix + " " + V(fd) + ", " + V(fs) + ", $i";
    case Vmulq:
        return name + suffix + " " + V(fd) + ", " + V(fs) + ", $q";
    case Vmaxi: case Vmuli: case Vminii:
        return name + suffix + " " + V(fd) + ", " + V(fs) + ", $i";
    case Vadd: case Vsub: case Vmadd: case Vmsub: case Vmul: case Vmax: case Vmini:
        return name + suffix + " " + V(fd) + ", " + V(fs) + ", " + V(ft);
    case Vopmsub:
        return "vopmsub.xyz " + V(fd) + ", " + V(fs) + ", " + V(ft);
    case Vadda: case Vmadda: case Vmula: case Vsuba: case Vmsuba:
        return name + suffix + " " + V(fs) + ", " + V(ft);
    case Vaddaq: case Vmaddaq: case Vsubaq: case Vmsubaq:
        return name + suffix + " " + V(fs) + ", $q";
    case Vaddai: case Vmaddai: case Vsubai: case Vmsubai:
        return name + suffix + " " + V(fs) + ", $i";
    case Vmulaq:
        return name + suffix + " " + V(fs) + ", $q";
    case Vmulai:
        return name + suffix + " " + V(fs) + ", $i";
    case Vopmula:
        return "vopmula.xyz " + V(fs) + ", " + V(ft);
    case Vabs: case Vmove: case Vmr32:
        return name + suffix + " " + V(ft) + ", " + V(fs);
    case Vitof0: case Vitof4: case Vitof12: case Vitof15:
    case Vftoi0: case Vftoi4: case Vftoi12: case Vftoi15:
        return name + suffix + " " + V(ft) + ", " + V(fs);
    case Vclip:
        return "vclip " + V(fs) + ", " + V(ft);
    case Viadd: case Visub: case Viand: case Vior:
        return name + " " + I(fd) + ", " + I(fs) + ", " + I(ft);
    case Viaddi:
        return name + " " + I(ft) + ", " + I(fs) + ", " + signed_dec(i.imm5());
    case Vcallms:
        return "vcallms " + hex(i.imm15() * 8u, 5);
    case Vcallmsr:
        return "vcallmsr";
    case Vnop:
        return "vnop";
    case Vwaitq:
        return "vwaitq";
    case Vlqi:
        return "vlqi " + V(ft) + ", (" + I(fs) + "++)";
    case Vsqi:
        return "vsqi " + V(fs) + ", (" + I(ft) + "++)";
    case Vlqd:
        return "vlqd " + V(ft) + ", (--" + I(fs) + ")";
    case Vsqd:
        return "vsqd " + V(fs) + ", (--" + I(ft) + ")";
    case Vdiv:
        return "vdiv $q, " + V(fs) + "." + lane_letter(fsf) + ", " + V(ft) + "." + lane_letter(ftf);
    case Vsqrt:
        return "vsqrt $q, " + V(ft) + "." + lane_letter(ftf);
    case Vrsqrt:
        return "vrsqrt $q, " + V(fs) + "." + lane_letter(fsf) + ", " + V(ft) + "." + lane_letter(ftf);
    case Vmtir:
        return "vmtir " + I(ft) + ", " + V(fs) + "." + lane_letter(fsf);
    case Vmfir:
        return "vmfir " + V(ft) + ", " + I(fs);
    case Vilwr:
        return "vilwr " + I(ft) + ", (" + I(fs) + ")";
    case Viswr:
        return "viswr " + I(ft) + ", (" + I(fs) + ")";
    case Vrnext:
        return "vrnext" + suffix + " " + V(ft);
    case Vrget:
        return "vrget" + suffix + " " + V(ft);
    case Vrinit:
        return "vrinit " + V(fs) + "." + lane_letter(fsf);
    case Vrxor:
        return "vrxor " + V(fs) + "." + lane_letter(fsf);
    default:
        return "";
    }
}

class Listing {
public:
    Listing(const Image &image, const Program &program, std::FILE *out)
        : image_(image), program_(program), out_(out) {
        build_names();
    }

    void build_names();
    std::string label(uint32_t address);
    std::string mnemonic(const Insn &insn) const;
    std::string operands(const Insn &insn);
    void collect_branch_targets(uint32_t begin, uint32_t end);
    void emit(uint32_t begin, uint32_t end);
    void write_functions(const std::string &path);

    std::size_t code_words() const { return code_words_; }
    std::size_t data_words() const { return data_words_; }
    std::size_t invalid_words() const { return invalid_words_; }
    std::size_t label_count() const { return labels_.size(); }
    std::size_t function_count() const { return function_starts_; }
    std::size_t entry_count() const { return entry_starts_; }

private:
    void flush_data(uint32_t &run_start, std::vector<uint32_t> &run);

    const Image &image_;
    const Program &program_;
    std::FILE *out_;
    std::map<uint32_t, std::string> names_;
    std::set<uint32_t> labels_;
    std::set<uint32_t> branch_targets_;
    std::size_t code_words_{};
    std::size_t data_words_{};
    std::size_t invalid_words_{};
    std::size_t function_starts_{};
    std::size_t entry_starts_{};
};

void Listing::build_names() {
    auto add = [&](uint32_t address, std::string name) {
        if (address < program_.base || address >= program_.end) return;
        names_.emplace(address, std::move(name));
    };
    add(image_.entry(), "_start");
    for (const auto &[address, name] : image_.symbols()) add(address, name);
    // Program::entries also holds every place after a call and every branch
    // target; only the function starts are worth a name of their own here. The
    // branch targets get a .L_ label from the listing itself.
    for (uint32_t address : program_.weak) add(address, "sub_" + addr8(address));
    for (uint32_t address : program_.functions) {
        auto it = names_.find(address);
        if (it != names_.end() && it->second.rfind("sub_", 0) != 0) continue;
        names_.erase(address);
        add(address, "func_" + addr8(address));
    }
    labels_ = std::set<uint32_t>();
    for (const auto &[address, name] : names_) labels_.insert(address);
    function_starts_ = program_.functions.size();
    entry_starts_ = program_.entries.size();
}

std::string Listing::label(uint32_t address) {
    auto it = names_.find(address);
    if (it != names_.end()) return it->second;
    if (branch_targets_.count(address)) {
        static std::map<uint32_t, std::string> local;
        auto found = local.find(address);
        if (found == local.end()) found = local.emplace(address, ".L_" + addr8(address)).first;
        return found->second;
    }
    return hex(address, 8);
}

void Listing::collect_branch_targets(uint32_t begin, uint32_t end) {
    for (uint32_t pc = begin; pc + 4 <= end; pc += 4) {
        if (!program_.is_code(pc)) continue;
        const Insn insn = decode(*image_.word(pc), pc);
        if (insn.op == Op::Invalid) continue;
        const Flow f = flow(insn.op);
        if (f == Flow::Branch || f == Flow::BranchLikely || f == Flow::Jump || f == Flow::Call) {
            const uint32_t target = insn.branch_target_of();
            if (program_.is_code(target)) branch_targets_.insert(target);
        }
    }
}

std::string Listing::mnemonic(const Insn &insn) const {
    switch (insn.op) {
    case CF: return (insn.word & 1u) ? "c.un.s" : "c.f.s";
    case CEq: return (insn.word & 1u) ? "c.ueq.s" : "c.eq.s";
    case CLt: return (insn.word & 1u) ? "c.ult.s" : "c.lt.s";
    case CLe: return (insn.word & 1u) ? "c.ule.s" : "c.le.s";
    case Pmfhl: {
        static const char *const suffix[] = {".lw", ".uw", ".slw", ".lh", ".sh"};
        const unsigned sa = insn.sa();
        return "pmfhl" + std::string(sa < 5 ? suffix[sa] : "");
    }
    case Pmthl: return "pmthl.lw";
    default: return mnemonics().names[static_cast<std::size_t>(insn.op)];
    }
}

std::string Listing::operands(const Insn &insn) {
    const unsigned rs = insn.rs(), rt = insn.rt(), rd = insn.rd(), sa = insn.sa();
    const unsigned fs = insn.fs(), ft = insn.ft(), fd = insn.fd();

    auto three = [&] { return gpr(rd) + ", " + gpr(rs) + ", " + gpr(rt); };
    auto load_store = [&](const std::string &reg) { return reg + ", " + simm(insn.simm()) + "(" + gpr(rs) + ")"; };
    auto branch = [&] { return label(insn.branch_target()); };
    auto branch2 = [&] { return gpr(rs) + ", " + gpr(rt) + ", " + branch(); };

    switch (insn.op) {
    // ---- SPECIAL -----------------------------------------------------------
    case Sll: case Srl: case Sra: case Dsll: case Dsrl: case Dsra:
        return gpr(rd) + ", " + gpr(rt) + ", " + std::to_string(sa);
    case Dsll32: case Dsrl32: case Dsra32:
        return gpr(rd) + ", " + gpr(rt) + ", " + std::to_string(sa + 32u);
    case Sllv: case Srlv: case Srav: case Dsllv: case Dsrlv: case Dsrav:
        return gpr(rd) + ", " + gpr(rt) + ", " + gpr(rs);
    case Jr: return gpr(rs);
    case Jalr: return gpr(rd) + ", " + gpr(rs);
    case Movz: case Movn:
        return gpr(rd) + ", " + gpr(rs) + ", " + gpr(rt);
    case Syscall: case Break: {
        const unsigned code = (insn.word >> 6) & 0xFFFFFu;
        return code ? hex(code) : std::string();
    }
    case Sync: {
        return sa ? std::to_string(sa) : std::string();
    }
    case Mfhi: case Mflo: case Mfsa:
        return gpr(rd);
    case Mthi: case Mtlo: case Mtsa:
        return gpr(rs);
    case Mult: case Multu: case Div: case Divu:
        return gpr(rs) + ", " + gpr(rt);
    case Add: case Addu: case Sub: case Subu: case And: case Or: case Xor: case Nor:
    case Slt: case Sltu: case Dadd: case Daddu: case Dsub: case Dsubu:
        return three();
    case Tge: case Tgeu: case Tlt: case Tltu: case Teq: case Tne:
        return gpr(rs) + ", " + gpr(rt);

    // ---- REGIMM ------------------------------------------------------------
    case Bltz: case Bgez: case Bltzl: case Bgezl: case Bltzal: case Bgezal:
    case Bltzall: case Bgezall:
        return gpr(rs) + ", " + branch();
    case Tgei: case Tgeiu: case Tlti: case Tltiu: case Teqi: case Tnei:
        return gpr(rs) + ", " + simm(insn.simm());
    case Mtsab: case Mtsah:
        return gpr(rs) + ", " + std::to_string(insn.simm());

    // ---- PRIMARY -----------------------------------------------------------
    case J: case Jal:
        return label(insn.jump_target());
    case Beq: case Bne:
        return branch2();
    case Blez: case Bgtz: case Beql: case Bnel: case Blezl: case Bgtzl:
        return gpr(rs) + ", " + branch();
    case Addi: case Addiu: case Slti: case Sltiu: case Daddi: case Daddiu:
        return gpr(rt) + ", " + gpr(rs) + ", " + simm(insn.simm());
    case Andi: case Ori: case Xori:
        return gpr(rt) + ", " + gpr(rs) + ", " + hex(insn.uimm());
    case Lui:
        return gpr(rt) + ", " + hex(insn.uimm());
    case Lb: case Lh: case Lw: case Lbu: case Lhu: case Lwu: case Ld:
    case Sb: case Sh: case Sw: case Sd:
    case Lwl: case Lwr: case Swl: case Swr: case Ldl: case Ldr: case Sdl: case Sdr:
    case Lq: case Sq:
        return load_store(gpr(rt));
    case Cache:
        return std::to_string(rt) + ", " + simm(insn.simm()) + "(" + gpr(rs) + ")";
    case Pref:
        return std::to_string(rt) + ", " + simm(insn.simm()) + "(" + gpr(rs) + ")";
    case Lwc1: case Swc1:
        return "$f" + std::to_string(ft) + ", " + simm(insn.simm()) + "(" + gpr(rs) + ")";
    case Lqc2: case Sqc2:
        return "$vf" + std::to_string(ft) + ", " + simm(insn.simm()) + "(" + gpr(rs) + ")";

    // ---- MMI ---------------------------------------------------------------
    case Madd: case Maddu: case Mult1: case Multu1: case Div1: case Divu1:
    case Madd1: case Maddu1: case Pdivw: case Pdivuw: case Pdivbw:
        return gpr(rs) + ", " + gpr(rt);
    case Plzcw:
        return gpr(rd) + ", " + gpr(rs);
    case Mfhi1: case Mflo1: case Pmfhi: case Pmflo:
        return gpr(rd);
    case Mthi1: case Mtlo1: case Pmthi: case Pmtlo:
        return gpr(rs);
    case Pmfhl:
        return gpr(rd);
    case Pmthl:
        return gpr(rs);
    case Psllh: case Psrlh: case Psrah: case Psllw: case Psrlw: case Psraw:
        return gpr(rd) + ", " + gpr(rt) + ", " + std::to_string(sa);
    case Paddw: case Psubw: case Pcgtw: case Pmaxw:
    case Paddh: case Psubh: case Pcgth: case Pmaxh:
    case Paddb: case Psubb: case Pcgtb:
    case Paddsw: case Psubsw: case Pextlw: case Ppacw:
    case Paddsh: case Psubsh: case Pextlh: case Ppach:
    case Paddsb: case Psubsb: case Pextlb: case Ppacb:
    case Pceqw: case Pminw: case Padsbh:
    case Pceqh: case Pminh: case Pceqb:
    case Padduw: case Psubuw: case Pextuw:
    case Padduh: case Psubuh: case Pextuh:
    case Paddub: case Psubub: case Pextub:
    case Pand: case Pxor: case Por: case Pnor:
    case Pinth: case Pinteh: case Pcpyld: case Pcpyud:
    case Psllvw: case Psrlvw: case Psravw: case Qfsrv:
        return three();
    case Pext5: case Ppac5: case Pabsw: case Pabsh:
    case Pexeh: case Prevh: case Pexew: case Prot3w:
    case Pexch: case Pcpyh: case Pexcw:
        return gpr(rd) + ", " + gpr(rt);
    case Pmaddw: case Pmsubw: case Pmadduw: case Pmultw: case Pmultuw:
    case Pmulth: case Pmaddh: case Pmsubh: case Phmadh: case Phmsbh:
        return gpr(rd) + ", " + gpr(rs) + ", " + gpr(rt);

    // ---- COP0 --------------------------------------------------------------
    case Mfc0: case Mtc0:
        return gpr(rt) + ", $" + std::to_string(rd);
    case Bc0f: case Bc0t: case Bc0fl: case Bc0tl:
        return branch();
    case Tlbr: case Tlbwi: case Tlbwr: case Tlbp: case Eret:
        return "";
    case Ei: case Di:
        return gpr(rt);

    // ---- COP1 --------------------------------------------------------------
    case Mfc1: case Cfc1: case Mtc1: case Ctc1:
        return gpr(rt) + ", $f" + std::to_string(fs);
    case Bc1f: case Bc1t: case Bc1fl: case Bc1tl:
        return branch();
    case AddS: case SubS: case MulS: case DivS: case MaxS: case MinS:
    case MaddS: case MsubS: case MaddaS: case MsubaS:
        return "$f" + std::to_string(fd) + ", $f" + std::to_string(fs) + ", $f" + std::to_string(ft);
    case SqrtS:
        return "$f" + std::to_string(fd) + ", $f" + std::to_string(ft);
    case AbsS: case MovS: case NegS: case CvtWS: case CvtSW:
        return "$f" + std::to_string(fd) + ", $f" + std::to_string(fs);
    case RsqrtS:
        return "$f" + std::to_string(fd) + ", $f" + std::to_string(fs) + ", $f" + std::to_string(ft);
    case AddaS: case SubaS: case MulaS:
        return "$f" + std::to_string(fs) + ", $f" + std::to_string(ft);
    case CF: case CEq: case CLt: case CLe:
        return "$f" + std::to_string(fs) + ", $f" + std::to_string(ft);

    // ---- COP2 / VU macro mode ---------------------------------------------
    case Qmfc2: case Qmtc2:
        return gpr(rt) + ", $vf" + std::to_string(fs);
    case Cfc2: case Ctc2:
        return gpr(rt) + ", $vi" + std::to_string(fs & 15u);
    case Bc2f: case Bc2t: case Bc2fl: case Bc2tl:
        return branch();
    default: {
        const std::string text = vu_macro(insn);
        return text.empty() ? std::string("???") : text;
    }
    }
}

void Listing::flush_data(uint32_t &run_start, std::vector<uint32_t> &run) {
    if (run.empty()) return;
    std::size_t at = 0;
    while (at < run.size()) {
        // Four words per line, aligned to a 16-byte boundary.
        const std::size_t room = (16u - (run_start & 15u)) / 4u;
        const std::size_t count = std::min<std::size_t>({4u, room, run.size() - at});
        std::fprintf(out_, "%08x  .word ", run_start + static_cast<uint32_t>(at) * 4u);
        for (std::size_t i = 0; i < count; ++i) {
            if (i) std::fputs(", ", out_);
            std::fprintf(out_, "0x%08x", run[at + i]);
        }
        std::fputc('\n', out_);
        at += count;
    }
    data_words_ += run.size();
    run_start = 0;
    run.clear();
}

void Listing::emit(uint32_t begin, uint32_t end) {
    collect_branch_targets(begin, end);

    for (uint32_t address : branch_targets_) {
        if (!names_.count(address)) labels_.insert(address);
    }

    std::fprintf(out_, "# Disassembly generated by scripts/disas (ps2-native-recomp)\n");
    std::fprintf(out_, "# %08x .. %08x\n", begin, end);
    std::fprintf(out_, ".set noreorder\n");
    std::fprintf(out_, ".set noat\n\n");

    uint32_t data_start = 0;
    std::vector<uint32_t> data;
    uint32_t zeros_start = 0;
    uint32_t zero_run = 0;

    auto flush = [&] { flush_data(data_start, data); };
    auto flush_zeros = [&] {
        if (!zero_run) return;
        if (zero_run >= 8) {
            flush();
            std::fprintf(out_, "%08x  .zero 0x%x\n", zeros_start, zero_run * 4u);
            data_words_ += zero_run;
        } else {
            // A zero word the search marked as code is a real NOP (a delay slot,
            // padding inside a function). A run of them is the gap between
            // functions, and the search's pointer scan often mistakes it for
            // code; list it as data instead.
            for (uint32_t i = 0; i < zero_run; ++i) {
                const uint32_t at = zeros_start + i * 4u;
                if (program_.is_code(at) && !names_.count(at) && !branch_targets_.count(at)) {
                    flush();
                    std::fprintf(out_, "%08x  00000000  %-12s\n", at, "nop");
                    ++code_words_;
                } else {
                    if (data.empty()) data_start = at;
                    data.push_back(0);
                    if (data.size() >= 4) flush();
                }
            }
        }
        zero_run = 0;
    };

    for (uint32_t pc = begin; pc + 4 <= end; pc += 4) {
        const uint32_t value = *image_.word(pc);
        const bool labelled = names_.count(pc) || branch_targets_.count(pc);
        if (value == 0 && !labelled) {
            if (zero_run == 0) zeros_start = pc;
            ++zero_run;
            continue;
        }
        flush_zeros();
        if (program_.is_code(pc)) {
            flush();
            auto name = names_.find(pc);
            if (name != names_.end()) {
                const bool is_function = name->second.rfind("func_", 0) == 0 || name->second == "_start";
                if (is_function) std::fputc('\n', out_);
                std::fprintf(out_, "%s:\n", name->second.c_str());
            } else if (branch_targets_.count(pc)) {
                std::fprintf(out_, ".L_%08x:\n", pc);
            }
            const Insn insn = decode(value, pc);
            ++code_words_;
            if (insn.op == Op::Invalid) {
                std::fprintf(out_, "%08x  .word 0x%08x                  # invalid instruction\n", pc, insn.word);
                ++invalid_words_;
            } else {
                std::fprintf(out_, "%08x  %08x  %-12s %s\n", pc, insn.word, mnemonic(insn).c_str(), operands(insn).c_str());
            }
        } else {
            if (data.empty()) data_start = pc;
            data.push_back(value);
            if (data.size() >= 4) flush();
        }
    }
    flush_zeros();
    flush();
}

void Listing::write_functions(const std::string &path) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) return;
    for (const auto &[address, name] : names_) {
        if (!program_.is_code(address)) continue;
        const char *kind = name.rfind("func_", 0) == 0 ? "func" :
                           name.rfind("sub_", 0) == 0 ? "sub" :
                           name.rfind("loc_", 0) == 0 ? "loc" : "name";
        std::fprintf(file, "%08x %-4s %s\n", address, kind, name.c_str());
    }
    std::fclose(file);
}

void usage() {
    std::fprintf(stderr,
        "usage: disas <elf|raw> [--base HEX] [--entry HEX]... [--entries FILE]\n"
        "             [--out FILE] [--start HEX] [--end HEX] [--stats FILE]\n"
        "             [--funcs FILE] [--quiet]\n");
}

bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) return false;
    out.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!out.empty()) file.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}

bool parse_hex(const char *text, uint32_t &value) {
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 16);
    if (end == text || *end != '\0' || parsed > 0xFFFFFFFFull) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

std::string base_name(const std::string &path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    const std::string input = argv[1];
    std::string out_path = "-";
    std::string stats_path, funcs_path, entries_path;
    std::vector<uint32_t> extra_entries;
    uint32_t base = 0, start = 0, end = 0;
    bool have_base = false, have_start = false, have_end = false, quiet = false;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--out") { if (const char *v = next()) out_path = v; }
        else if (arg == "--stats") { if (const char *v = next()) stats_path = v; }
        else if (arg == "--funcs") { if (const char *v = next()) funcs_path = v; }
        else if (arg == "--entries") { if (const char *v = next()) entries_path = v; }
        else if (arg == "--entry") { const char *v = next(); uint32_t a; if (v && parse_hex(v, a)) extra_entries.push_back(a); }
        else if (arg == "--base") { const char *v = next(); if (v && parse_hex(v, base)) have_base = true; }
        else if (arg == "--start") { const char *v = next(); if (v && parse_hex(v, start)) have_start = true; }
        else if (arg == "--end") { const char *v = next(); if (v && parse_hex(v, end)) have_end = true; }
        else if (arg == "--quiet") quiet = true;
        else if (arg == "--help" || arg == "-h") { usage(); return 0; }
        else {
            std::fprintf(stderr, "disas: unknown option %s\n", arg.c_str());
            return 1;
        }
    }

    std::vector<uint8_t> bytes;
    if (!read_file(input, bytes)) {
        std::fprintf(stderr, "disas: cannot read %s\n", input.c_str());
        return 1;
    }

    const bool is_elf = bytes.size() >= 4 && std::memcmp(bytes.data(), "\x7f" "ELF", 4) == 0;
    Image image = [&] {
        if (is_elf) return Image::load(input);
        if (!have_base) {
            std::fprintf(stderr, "disas: a raw image needs --base\n");
            std::exit(1);
        }
        const auto text = std::make_pair(base, static_cast<uint32_t>(base + bytes.size()));
        return Image::raw(base, std::move(bytes), text);
    }();

    // entries.txt: bare addresses feed an ELF; "BASE_CRC:OFFSET" lines feed the
    // raw overlay whose base matches (the CRC part only separates variants).
    if (!entries_path.empty()) {
        std::ifstream entries(entries_path);
        std::string line;
        const std::string stem = base_name(input);
        while (std::getline(entries, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) {
                uint32_t address;
                if (parse_hex(line.c_str(), address)) extra_entries.push_back(address);
                continue;
            }
            const std::string prefix = line.substr(0, colon);
            const std::string offset_text = line.substr(colon + 1);
            if (prefix.size() < 8) continue;
            uint32_t line_base;
            if (!parse_hex(prefix.substr(0, 8).c_str(), line_base)) continue;
            if (!is_elf) {
                const bool same_stem = stem.rfind(prefix, 0) == 0;
                if (!same_stem && line_base != base) continue;
            } else {
                if (line_base != base) continue;
            }
            uint32_t offset;
            if (parse_hex(offset_text.c_str(), offset)) extra_entries.push_back(line_base + offset);
        }
    }

    // Extra entries come from pointer scans and tables: they are hints, not
    // proven code. Hand them to the search as candidates so that a table of
    // floats or strings is not walked as instructions; only the ELF entry is a
    // root that is trusted without a decode check.
    std::vector<uint32_t> roots;
    if (is_elf) roots.push_back(image.entry());

    Search search;
    search.whole_image = true;
    search.after_return = false;
    search.candidates = extra_entries;
    const Program program = analyse(image, roots, {}, search);

    if (!have_start) start = program.base;
    // Program::end covers the whole BSS (MemSiz); the listing stops at the last
    // byte the ELF actually carries, so that a 24 MB .bss is not 24 MB of .zero.
    uint32_t initialized_end = program.base;
    for (const auto &segment : image.segments())
        if (segment.executable && segment.initialized_size)
            initialized_end = std::max(initialized_end, segment.address + segment.initialized_size);
    if (!have_end) end = initialized_end;
    start &= ~3u;
    end = std::min(end, initialized_end);
    end &= ~3u;
    if (start >= end) {
        std::fprintf(stderr, "disas: empty range %08x..%08x\n", start, end);
        return 1;
    }

    std::FILE *out = stdout;
    if (out_path != "-") {
        out = std::fopen(out_path.c_str(), "wb");
        if (!out) {
            std::fprintf(stderr, "disas: cannot write %s\n", out_path.c_str());
            return 1;
        }
    }

    Listing listing(image, program, out);
    listing.emit(start, end);

    if (out != stdout) std::fclose(out);

    if (!funcs_path.empty()) listing.write_functions(funcs_path);

    const std::size_t words = (end - start) / 4u;
    const std::size_t code = listing.code_words(), data = listing.data_words();
    if (!stats_path.empty()) {
        std::FILE *stats = std::fopen(stats_path.c_str(), "wb");
        if (stats) {
            std::fprintf(stats, "input %s\n", input.c_str());
            std::fprintf(stats, "base %08x\nend %08x\nbytes %zu\n", start, end, static_cast<std::size_t>(end - start));
            std::fprintf(stats, "words %zu\ncode_words %zu\ndata_words %zu\ninvalid_words %zu\n",
                         words, code, data, listing.invalid_words());
            std::fprintf(stats, "functions %zu\nentries %zu\nlabels %zu\n",
                         listing.function_count(), listing.entry_count(), listing.label_count());
            std::fclose(stats);
        }
    }
    if (!quiet) {
        std::fprintf(stderr, "disas: %08x..%08x  %zu words  %zu code  %zu data  %zu invalid\n",
                     start, end, words, code, data, listing.invalid_words());
        std::fprintf(stderr, "disas: %zu functions, %zu entries, %zu labels\n",
                     listing.function_count(), listing.entry_count(), listing.label_count());
    }
    return 0;
}
