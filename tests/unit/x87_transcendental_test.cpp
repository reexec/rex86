// The x87 transcendentals (#25). The host comparison fuzz judges the
// numbers against real hardware within an ulp tolerance; these tests pin
// what must hold exactly: the binary128 constants and kernels, the
// reduction by the x87's 66-bit Pi, the SDM's special-value tables, the
// behavior measured where the SDM is silent, and the register stack.

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "fpu/transcendental_kernels.h"
#include "fpu/x87_transcendental.h"
#include "interp/x87_stack.h"
#include "rex86/cpu.h"
#include "test_support.h"

namespace
{

using rex86::fpu::Float80;
using rex86::fpu::Status;
using rex86::fpu::TrigOutcome;
namespace detail = rex86::fpu::detail;
namespace fpu = rex86::fpu;

constexpr std::uint16_t kMasked = 0x037F;    // all masked, nearest
constexpr std::uint16_t kRoundDown = 0x077F;

constexpr Float80 kOne{0x8000000000000000ull, 0x3FFF};
// The 80-bit pi and pi/2 (FLDPI's nearest values).
constexpr Float80 kPi80{0xC90FDAA22168C235ull, 0x4000};
constexpr Float80 kHalfPi80{0xC90FDAA22168C235ull, 0x3FFF};

Float80 Value(const std::uint64_t significand, const std::uint16_t sign_exponent)
{
    return Float80{significand, sign_exponent};
}

// |a - b| <= 2^-bits * |b| for binary128 values, b nonzero.
bool Close(const float128_t a, const float128_t b, const int bits)
{
    const detail::NearestScope near;
    const float128_t diff = detail::Abs(f128_sub(a, b));
    return !f128_lt(detail::Ldexp(detail::Abs(b), -bits), diff);
}

void ConstantAndKernelTests(rex86::test::Context& context)
{
    const detail::NearestScope near;
    const float128_t one = i32_to_f128(1);
    // ln2 * log2(e) = 1 and 8 atan(1/8 ...) halvings give pi/4.
    REX86_CHECK(context, Close(f128_mul(detail::Ln2(), detail::Log2E()), one, 110));
    REX86_CHECK(context,
                Close(detail::AtanKernel(one, i32_to_f128(0)).value, detail::PiOver4(), 108));
    REX86_CHECK(context, Close(f128_mul(detail::Sqrt2(), detail::Sqrt2()), i32_to_f128(2), 111));
    // sin^2 + cos^2 = 1 and tan = sin/cos on a few reduced arguments.
    for (const int k : {-70, -30, -8, -2, -1})
    {
        const float128_t theta = f128_mul(detail::Ldexp(one, k), i32_to_f128(3));
        const float128_t s = detail::SinKernel(theta).value;
        const float128_t c = detail::CosKernel(theta).value;
        REX86_CHECK(context, Close(f128_add(f128_mul(s, s), f128_mul(c, c)), one, 108));
        REX86_CHECK(context, Close(detail::TanKernel(theta).value, f128_div(s, c), 108));
        REX86_CHECK(context, Close(detail::CotKernel(theta).value, f128_div(c, s), 108));
    }
    // 2^1 - 1 = 1, 2^-1 - 1 = -1/2; log2 of 2^5 is exactly 5.
    REX86_CHECK(context, Close(detail::Exp2Minus1Kernel(one), one, 108));
    REX86_CHECK(context, Close(detail::Exp2Minus1Kernel(detail::Negate(one)),
                               detail::Negate(detail::Ldexp(one, -1)), 108));
    REX86_CHECK(context, f128_eq(detail::Log2Kernel(one, 5), i32_to_f128(5)));
    // log2(3) = log2(1.5) + 1 both ways.
    REX86_CHECK(context, Close(detail::Log2Kernel(f128_div(i32_to_f128(3), i32_to_f128(2)), 1),
                               f128_add(one, detail::Log2OnePlusKernel(detail::Ldexp(one, -1))),
                               108));

    // The 80-bit pi is Pi + 2^-64 (Pi = C90FDAA2 2168C234 C), pi/2 is
    // Pi/2 + 2^-65: the reduction leaves exactly those.
    const detail::Reduced pi = detail::ReduceByPi66(detail::Normalize(kPi80));
    REX86_CHECK_EQ(context, pi.quadrant, 2u);
    REX86_CHECK(context, f128_eq(pi.theta, detail::Ldexp(one, -64)));
    const detail::Reduced half = detail::ReduceByPi66(detail::Normalize(kHalfPi80));
    REX86_CHECK_EQ(context, half.quadrant, 1u);
    REX86_CHECK(context, f128_eq(half.theta, detail::Ldexp(one, -65)));
    const Float80 below{0xC90FDAA22168C234ull, 0x4000};  // Pi - 3 * 2^-64
    const detail::Reduced under = detail::ReduceByPi66(detail::Normalize(below));
    REX86_CHECK_EQ(context, under.quadrant, 2u);
    REX86_CHECK(context, f128_eq(under.theta, detail::Negate(f128_mul(
                                                  detail::Ldexp(one, -64), i32_to_f128(3)))));
}

void TrigonometricTests(rex86::test::Context& context)
{
    {
        // SDM Vol. 1, 8.3.8: the result follows the 66-bit Pi, measured
        // bit for bit on an Intel Kaby Lake.
        Status s(kMasked);
        Float80 r;
        REX86_CHECK(context, fpu::Sine(&s, kPi80, &r) == TrigOutcome::kWritten);
        REX86_CHECK(context, r == Value(0x8000000000000000ull, 0xBFBF));  // -2^-64
        REX86_CHECK(context, (s.raised & fpu::kPrecision) != 0 && s.round_up);
        Status c(kMasked);
        REX86_CHECK(context, fpu::Cosine(&c, kHalfPi80, &r) == TrigOutcome::kWritten);
        REX86_CHECK(context, r == Value(0x8000000000000000ull, 0xBFBE));  // -2^-65
        Status t(kMasked);
        REX86_CHECK(context, fpu::Tangent(&t, kHalfPi80, &r) == TrigOutcome::kWritten);
        REX86_CHECK(context, r == Value(0x8000000000000000ull, 0xC040));  // -2^65
        Status p(kMasked);
        REX86_CHECK(context, fpu::Sine(&p, Value(0xC90FDAA22168C234ull, 0x4000), &r) ==
                                 TrigOutcome::kWritten);
        REX86_CHECK(context, r == Value(0xC000000000000000ull, 0x3FC0));  // 3 * 2^-64
    }
    {
        // The range edge: |x| >= 2^63 is left alone, without exceptions.
        Status s(kMasked);
        Float80 r{};
        const Float80 edge{0x8000000000000000ull, 0x403E};
        REX86_CHECK(context, fpu::Sine(&s, edge, &r) == TrigOutcome::kOutOfRange);
        REX86_CHECK_EQ(context, s.raised, std::uint16_t{0});
        Status below(kMasked);
        REX86_CHECK(context, fpu::Sine(&below, Value(~0ull, 0x403D), &r) == TrigOutcome::kWritten);
    }
    {
        // Zeros are exact; infinities are #IA.
        Status s(kMasked);
        Float80 r;
        fpu::Sine(&s, Value(0, 0x8000), &r);
        REX86_CHECK(context, r == Value(0, 0x8000) && s.raised == 0);
        fpu::Cosine(&s, Value(0, 0x8000), &r);
        REX86_CHECK(context, r == kOne && s.raised == 0);
        Status inf(kMasked);
        fpu::Tangent(&inf, Value(0x8000000000000000ull, 0x7FFF), &r);
        REX86_CHECK(context, r == fpu::kIndefinite && (inf.raised & fpu::kInvalid) != 0);
        Status unmasked(0x037E);
        REX86_CHECK(context, fpu::Sine(&unmasked, Value(0x8000000000000000ull, 0x7FFF), &r) ==
                                 TrigOutcome::kSuppressed);
    }
    {
        // Below 2^-68 the argument (or 1) comes back inexact with C1 = 0
        // in every rounding direction; a denormal one underflows.
        Status s(kRoundDown);
        Float80 r;
        const Float80 tiny{0x8000000000000000ull, 0x3FFF - 70};
        fpu::Sine(&s, tiny, &r);
        REX86_CHECK(context, r == tiny && s.raised == fpu::kPrecision && !s.round_up);
        Status c(kRoundDown);
        fpu::Cosine(&c, tiny, &r);
        REX86_CHECK(context, r == kOne && c.raised == fpu::kPrecision && !c.round_up);
        Status d(kMasked);
        const Float80 denormal{1, 0};
        fpu::Sine(&d, denormal, &r);
        REX86_CHECK(context, r == denormal);
        REX86_CHECK_EQ(context, d.raised,
                       std::uint16_t{fpu::kDenormalOperand | fpu::kUnderflow | fpu::kPrecision});
        Status u(0x036F);  // #U unmasked: the bias-adjusted 2^-16445 * 2^24576
        fpu::Sine(&u, denormal, &r);
        REX86_CHECK(context, r == Value(0x8000000000000000ull, 0x5FC2));
        Status pseudo(kMasked);  // a pseudo-denormal is not tiny
        fpu::Sine(&pseudo, Value(0x8000000000000001ull, 0x0000), &r);
        REX86_CHECK(context, r == Value(0x8000000000000001ull, 0x0001));
        REX86_CHECK_EQ(context, pseudo.raised,
                       std::uint16_t{fpu::kDenormalOperand | fpu::kPrecision});
        // At 2^-64 the computed path rounds sin correctly: down to x - ulp.
        Status rd(kRoundDown);
        fpu::Sine(&rd, Value(0x8000000000000000ull, 0x3FFF - 64), &r);
        REX86_CHECK(context, r == Value(~0ull, 0x3FFF - 65) && !rd.round_up);
    }
    {
        Status s(kMasked);
        Float80 sine;
        Float80 cosine;
        REX86_CHECK(context, fpu::SineCosine(&s, Value(0, 0), &sine, &cosine) ==
                                 TrigOutcome::kWritten);
        REX86_CHECK(context, sine == Value(0, 0) && cosine == kOne);
    }
}

void OtherFunctionTests(rex86::test::Context& context)
{
    Float80 r;
    {
        // atan(1) is pi/4 to nearest; the SDM table's quadrant values.
        Status s(kMasked);
        REX86_CHECK(context, fpu::Arctangent(&s, kOne, kOne, &r));
        REX86_CHECK(context, r == Value(0xC90FDAA22168C235ull, 0x3FFE));
        REX86_CHECK(context, (s.raised & fpu::kPrecision) != 0);
        Status z(kMasked);
        fpu::Arctangent(&z, Value(0, 0), Value(0, 0x8000), &r);  // atan2(+0, -0) = +pi
        REX86_CHECK(context, r == kPi80);
        fpu::Arctangent(&z, Value(0, 0x8000), Value(0, 0), &r);  // atan2(-0, +0) = -0
        REX86_CHECK(context, r == Value(0, 0x8000));
        fpu::Arctangent(&z, Value(0x8000000000000000ull, 0xFFFF),
                        Value(0x8000000000000000ull, 0x7FFF), &r);  // -pi/4
        REX86_CHECK(context, r == Value(0xC90FDAA22168C235ull, 0xBFFE));
    }
    {
        // F2XM1: +-1 exact but inexact-flagged; |x| > 1 returns x;
        // the infinities are exact.
        Status s(kRoundDown);
        REX86_CHECK(context, fpu::Exp2Minus1(&s, kOne, &r));
        REX86_CHECK(context, r == kOne && s.raised == fpu::kPrecision && !s.round_up);
        Status n(kMasked);
        fpu::Exp2Minus1(&n, Value(0x8000000000000000ull, 0xBFFF), &r);
        REX86_CHECK(context, r == Value(0x8000000000000000ull, 0xBFFE));
        Status o(kMasked);
        const Float80 one_half{0xC000000000000000ull, 0x3FFF};
        fpu::Exp2Minus1(&o, one_half, &r);
        REX86_CHECK(context, r == one_half && o.raised == fpu::kPrecision);
        Status i(kMasked);
        fpu::Exp2Minus1(&i, Value(0x8000000000000000ull, 0xFFFF), &r);
        REX86_CHECK(context, r == Value(0x8000000000000000ull, 0xBFFF) && i.raised == 0);
    }
    {
        // FYL2X: 3 * log2(8) = 9; log2(0) is #Z; a negative x is #IA.
        Status s(kMasked);
        REX86_CHECK(context, fpu::YLog2X(&s, Value(0xC000000000000000ull, 0x4000),
                                         Value(0x8000000000000000ull, 0x4002), &r));
        REX86_CHECK(context, r == Value(0x9000000000000000ull, 0x4002));
        Status z(kMasked);
        fpu::YLog2X(&z, Value(0x8000000000000000ull, 0x4000), Value(0, 0), &r);
        REX86_CHECK(context, r == Value(0x8000000000000000ull, 0xFFFF));
        REX86_CHECK_EQ(context, z.raised, std::uint16_t{fpu::kZeroDivide});
        Status zu(0x037B);
        REX86_CHECK(context, !fpu::YLog2X(&zu, kOne, Value(0, 0), &r));
        Status neg(kMasked);
        fpu::YLog2X(&neg, kOne, Value(0x8000000000000000ull, 0xBFFF), &r);
        REX86_CHECK(context, r == fpu::kIndefinite);
        // An exact denormal product is reported inexact, so it underflows
        // (measured, #25): y * log2(2) = y.
        Status tiny(kMasked);
        const Float80 denormal{0x0123456789ABCDEFull, 0};
        fpu::YLog2X(&tiny, denormal, Value(0x8000000000000000ull, 0x4000), &r);
        REX86_CHECK(context, r == denormal);
        REX86_CHECK_EQ(context, tiny.raised,
                       std::uint16_t{fpu::kDenormalOperand | fpu::kUnderflow | fpu::kPrecision});
    }
    {
        // FYL2XP1 at x <= -1: x for a finite y, y's sign flipped for a
        // zero or infinite one (measured, #25).
        const Float80 minus3{0xC000000000000000ull, 0xC000};
        Status s(kMasked);
        fpu::YLog2XPlus1(&s, Value(0xA000000000000000ull, 0x4000), minus3, &r);
        REX86_CHECK(context, r == minus3 && s.raised == fpu::kPrecision);
        Status z(kMasked);
        fpu::YLog2XPlus1(&z, Value(0, 0), minus3, &r);
        REX86_CHECK(context, r == Value(0, 0x8000) && z.raised == 0);
    }
}

class Host final : public rex86::Environment
{
public:
    bool LoadDescriptor(std::uint16_t, rex86::Descriptor* descriptor) override
    {
        *descriptor = rex86::Descriptor{};
        return true;
    }
    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override { return false; }
    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override { return false; }
    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override { return false; }
    std::uint64_t ReadTimeStampCounter() override { return 0; }
    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t r[4]) override
    {
        r[0] = r[1] = r[2] = r[3] = 0;
    }
};

struct Machine
{
    std::vector<std::uint8_t> buffer;
    rex86::GuestMemory memory;
    Host host;
    rex86::Cpu cpu;

    explicit Machine(const std::initializer_list<std::uint8_t> program)
        : buffer(16 * rex86::kGuestPageSize, 0),
          memory(buffer.data(), static_cast<std::uint32_t>(buffer.size())),
          cpu(&memory, &host, rex86::Features{})
    {
        memory.pages().Set(0, static_cast<std::uint32_t>(buffer.size()),
                           rex86::kPageReadWriteExecute);
        std::uint32_t at = 0x1000;
        for (const std::uint8_t byte : program) buffer[at++] = byte;
        cpu.state().eip = 0x1000;
        cpu.state().Set(rex86::Gpr::kEsp, 0xF000);
    }
};

void StackTests(rex86::test::Context& context)
{
    namespace x87 = rex86::interp::x87;
    {
        // FPTAN pushes 1.0 above the tangent; FSINCOS leaves the cosine
        // on top of the sine.
        Machine m({0xD9, 0xEE,  // fldz
                   0xD9, 0xF2,  // fptan
                   0xD9, 0xEE,  // fldz
                   0xD9, 0xFB,  // fsincos
                   0xF4});
        m.cpu.Run(100);
        const rex86::X87State& s = m.cpu.state().x87;
        REX86_CHECK(context, x87::Read(s, 0) == kOne);           // cos 0
        REX86_CHECK(context, x87::Read(s, 1) == Value(0, 0));    // sin 0
        REX86_CHECK(context, x87::Read(s, 2) == kOne);           // FPTAN's 1.0
        REX86_CHECK(context, x87::Read(s, 3) == Value(0, 0));    // tan 0
    }
    {
        // Out of range: C2 = 1, no push.
        Machine m({0xD9, 0xF2,  // fptan
                   0xF4});
        x87::Push(&m.cpu.state().x87, Value(0x8000000000000000ull, 0x403F));  // 2^64
        m.cpu.Run(100);
        const rex86::X87State& s = m.cpu.state().x87;
        REX86_CHECK(context, (s.status_word & x87::kC2) != 0);
        REX86_CHECK(context, x87::Read(s, 0) == Value(0x8000000000000000ull, 0x403F));
        REX86_CHECK(context, x87::IsEmpty(s, 1));
    }
    {
        // FPATAN writes ST(1) and pops; C0 and C3 are left alone.
        Machine m({0xD9, 0xE8,  // fld1
                   0xD9, 0xE8,  // fld1
                   0xD9, 0xF3,  // fpatan
                   0xF4});
        m.cpu.state().x87.status_word = x87::kC0 | x87::kC3;
        m.cpu.Run(100);
        const rex86::X87State& s = m.cpu.state().x87;
        REX86_CHECK(context, x87::Read(s, 0) == Value(0xC90FDAA22168C235ull, 0x3FFE));
        REX86_CHECK(context, x87::IsEmpty(s, 1));
        REX86_CHECK_EQ(context, s.status_word & (x87::kC0 | x87::kC3),
                       std::uint16_t{x87::kC0 | x87::kC3});
    }
}

}  // namespace

void RunX87TranscendentalTests(rex86::test::Context& context)
{
    ConstantAndKernelTests(context);
    TrigonometricTests(context);
    OtherFunctionTests(context);
    StackTests(context);
}
