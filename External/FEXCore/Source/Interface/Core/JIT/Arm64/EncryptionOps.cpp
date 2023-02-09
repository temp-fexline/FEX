/*
$info$
tags: backend|arm64
$end_info$
*/

#include "Interface/Core/ArchHelpers/CodeEmitter/Emitter.h"
#include "Interface/Core/JIT/Arm64/JITClass.h"
#include "Interface/IR/Passes/RegisterAllocationPass.h"

namespace FEXCore::CPU {
#define DEF_OP(x) void Arm64JITCore::Op_##x(IR::IROp_Header const *IROp, IR::NodeID Node)

DEF_OP(AESImc) {
  auto Op = IROp->C<IR::IROp_VAESImc>();
  aesimc(GetVReg(Node), GetVReg(Op->Vector.ID()));
}

DEF_OP(AESEnc) {
  const auto Op = IROp->C<IR::IROp_VAESEnc>();
  const auto OpSize = IROp->Size;

  const auto Dst = GetVReg(Node);
  const auto Key = GetVReg(Op->Key.ID());
  const auto State = GetVReg(Op->State.ID());

  LOGMAN_THROW_AA_FMT(OpSize == Core::CPUState::XMM_SSE_REG_SIZE,
                      "Currently only supports 128-bit operations.");

  eor(VTMP2.Q(), VTMP2.Q(), VTMP2.Q());
  mov(VTMP1.Q(), State.Q());
  aese(VTMP1, VTMP2);
  aesmc(VTMP1, VTMP1);
  eor(Dst.Q(), VTMP1.Q(), Key.Q());
}

DEF_OP(AESEncLast) {
  const auto Op = IROp->C<IR::IROp_VAESEncLast>();
  const auto OpSize = IROp->Size;

  const auto Dst = GetVReg(Node);
  const auto Key = GetVReg(Op->Key.ID());
  const auto State = GetVReg(Op->State.ID());

  LOGMAN_THROW_AA_FMT(OpSize == Core::CPUState::XMM_SSE_REG_SIZE,
                      "Currently only supports 128-bit operations.");

  eor(VTMP2.Q(), VTMP2.Q(), VTMP2.Q());
  mov(VTMP1.Q(), State.Q());
  aese(VTMP1, VTMP2);
  eor(Dst.Q(), VTMP1.Q(), Key.Q());
}

DEF_OP(AESDec) {
  const auto Op = IROp->C<IR::IROp_VAESDec>();
  const auto OpSize = IROp->Size;

  const auto Dst = GetVReg(Node);
  const auto Key = GetVReg(Op->Key.ID());
  const auto State = GetVReg(Op->State.ID());

  LOGMAN_THROW_AA_FMT(OpSize == Core::CPUState::XMM_SSE_REG_SIZE,
                      "Currently only supports 128-bit operations.");

  eor(VTMP2.Q(), VTMP2.Q(), VTMP2.Q());
  mov(VTMP1.Q(), State.Q());
  aesd(VTMP1, VTMP2);
  aesimc(VTMP1, VTMP1);
  eor(Dst.Q(), VTMP1.Q(), Key.Q());
}

DEF_OP(AESDecLast) {
  const auto Op = IROp->C<IR::IROp_VAESDecLast>();
  const auto OpSize = IROp->Size;

  const auto Dst = GetVReg(Node);
  const auto Key = GetVReg(Op->Key.ID());
  const auto State = GetVReg(Op->State.ID());

  LOGMAN_THROW_AA_FMT(OpSize == Core::CPUState::XMM_SSE_REG_SIZE,
                      "Currently only supports 128-bit operations.");

  eor(VTMP2.Q(), VTMP2.Q(), VTMP2.Q());
  mov(VTMP1.Q(), State.Q());
  aesd(VTMP1, VTMP2);
  eor(Dst.Q(), VTMP1.Q(), Key.Q());
}

DEF_OP(AESKeyGenAssist) {
  auto Op = IROp->C<IR::IROp_VAESKeyGenAssist>();

  ARMEmitter::ForwardLabel Constant;
  ARMEmitter::ForwardLabel PastConstant;

  // Do a "regular" AESE step
  eor(VTMP2.Q(), VTMP2.Q(), VTMP2.Q());
  mov(VTMP1.Q(), GetVReg(Op->Src.ID()).Q());
  aese(VTMP1, VTMP2);

  // Do a table shuffle to undo ShiftRows
  ldr(VTMP3.Q(), &Constant);

  // Now EOR in the RCON
  if (Op->RCON) {
    tbl(VTMP1.Q(), VTMP1.Q(), VTMP3.Q());

    LoadConstant(ARMEmitter::Size::i64Bit, TMP1, static_cast<uint64_t>(Op->RCON) << 32);
    dup(ARMEmitter::SubRegSize::i64Bit, VTMP2.Q(), TMP1);
    eor(GetVReg(Node).Q(), VTMP1.Q(), VTMP2.Q());
  }
  else {
    tbl(GetVReg(Node).Q(), VTMP1.Q(), VTMP3.Q());
  }

  b(&PastConstant);
  Bind(&Constant);
  dc64(0x040B0E01'0B0E0104ULL);
  dc64(0x0C030609'0306090CULL);
  Bind(&PastConstant);
}

DEF_OP(CRC32) {
  auto Op = IROp->C<IR::IROp_CRC32>();

  const auto Dst = GetReg(Node);
  const auto Src1 = GetReg(Op->Src1.ID());
  const auto Src2 = GetReg(Op->Src2.ID());

  switch (Op->SrcSize) {
    case 1:
      crc32cb(Dst.W(), Src1.W(), Src2.W());
      break;
    case 2:
      crc32ch(Dst.W(), Src1.W(), Src2.W());
      break;
    case 4:
      crc32cw(Dst.W(), Src1.W(), Src2.W());
      break;
    case 8:
      crc32cx(Dst, Src1, Src2);
      break;
    default: LOGMAN_MSG_A_FMT("Unknown CRC32 size: {}", Op->SrcSize);
  }
}

DEF_OP(PCLMUL) {
  const auto Op = IROp->C<IR::IROp_PCLMUL>();
  const auto OpSize = IROp->Size;

  const auto Dst  = GetVReg(Node);
  const auto Src1 = GetVReg(Op->Src1.ID());
  const auto Src2 = GetVReg(Op->Src2.ID());

  LOGMAN_THROW_AA_FMT(OpSize == Core::CPUState::XMM_SSE_REG_SIZE,
                      "Currently only supports 128-bit operations.");

  switch (Op->Selector) {
  case 0b00000000:
    pmull(ARMEmitter::SubRegSize::i128Bit, Dst.D(), Src1.D(), Src2.D());
    break;
  case 0b00000001:
    dup(ARMEmitter::SubRegSize::i64Bit, VTMP1.Q(), Src1.Q(), 1);
    pmull(ARMEmitter::SubRegSize::i128Bit, Dst.D(), VTMP1.D(), Src2.D());
    break;
  case 0b00010000:
    dup(ARMEmitter::SubRegSize::i64Bit, VTMP1.Q(), Src2.Q(), 1);
    pmull(ARMEmitter::SubRegSize::i128Bit, Dst.D(), VTMP1.D(), Src1.D());
    break;
  case 0b00010001:
    pmull2(ARMEmitter::SubRegSize::i128Bit, Dst.Q(), Src1.Q(), Src2.Q());
    break;
  default:
    LOGMAN_MSG_A_FMT("Unknown PCLMUL selector: {}", Op->Selector);
    break;
  }
}

#undef DEF_OP
}
