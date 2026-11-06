#include "ir/ir_inst.h"

namespace baltam {

std::unique_ptr<Instruction> Instruction::clone() const {
    switch (type()) {
    case Type::WorkspaceBarrier:
        return std::make_unique<WorkspaceBarrierInst>(
            static_cast<const WorkspaceBarrierInst&>(*this));
    case Type::Parameter:
        return std::make_unique<ParameterInst>(static_cast<const ParameterInst&>(*this));
    case Type::Copy:
        return std::make_unique<CopyInst>(static_cast<const CopyInst&>(*this));
    case Type::Const:
        return std::make_unique<ConstInst>(static_cast<const ConstInst&>(*this));
    case Type::Phi:
        return std::make_unique<PhiInst>(static_cast<const PhiInst&>(*this));
    case Type::Concat:
        return std::make_unique<ConcatInst>(static_cast<const ConcatInst&>(*this));
    case Type::CellConstruct:
        return std::make_unique<CellConstructInst>(static_cast<const CellConstructInst&>(*this));
    case Type::Colon:
        return std::make_unique<ColonInst>(static_cast<const ColonInst&>(*this));
    case Type::MagicColon:
        return std::make_unique<MagicColonInst>(static_cast<const MagicColonInst&>(*this));
    case Type::MagicEnd:
        return std::make_unique<MagicEndInst>(static_cast<const MagicEndInst&>(*this));
    case Type::GlobalDecl:
        return std::make_unique<GlobalDeclInst>(static_cast<const GlobalDeclInst&>(*this));
    case Type::PersistentDecl:
        return std::make_unique<PersistentDeclInst>(static_cast<const PersistentDeclInst&>(*this));
    case Type::Goto:
        return std::make_unique<GotoInst>(static_cast<const GotoInst&>(*this));
    case Type::Branch:
        return std::make_unique<BranchInst>(static_cast<const BranchInst&>(*this));
    case Type::Return:
        return std::make_unique<ReturnInst>(static_cast<const ReturnInst&>(*this));
    case Type::Apply:
        return std::make_unique<ApplyInst>(static_cast<const ApplyInst&>(*this));
    case Type::ValueApply:
        return std::make_unique<ValueApplyInst>(static_cast<const ValueApplyInst&>(*this));
    case Type::Call:
        return std::make_unique<CallInst>(static_cast<const CallInst&>(*this));
    case Type::Intrinsic:
        return std::make_unique<IntrinsicInst>(static_cast<const IntrinsicInst&>(*this));
    case Type::ParenReference:
        return std::make_unique<ParenReferenceInst>(static_cast<const ParenReferenceInst&>(*this));
    case Type::BraceReference:
        return std::make_unique<BraceReferenceInst>(static_cast<const BraceReferenceInst&>(*this));
    case Type::DotAccess:
        return std::make_unique<DotAccessInst>(static_cast<const DotAccessInst&>(*this));
    case Type::DotReference:
        return std::make_unique<DotReferenceInst>(static_cast<const DotReferenceInst&>(*this));
    case Type::ParenAssign:
        return std::make_unique<ParenAssignInst>(static_cast<const ParenAssignInst&>(*this));
    case Type::ParenDelete:
        return std::make_unique<ParenDeleteInst>(static_cast<const ParenDeleteInst&>(*this));
    case Type::BraceAssign:
        return std::make_unique<BraceAssignInst>(static_cast<const BraceAssignInst&>(*this));
    case Type::DotAssign:
        return std::make_unique<DotAssignInst>(static_cast<const DotAssignInst&>(*this));
    case Type::Unary:
        return std::make_unique<UnaryInst>(static_cast<const UnaryInst&>(*this));
    case Type::Binary:
        return std::make_unique<BinaryInst>(static_cast<const BinaryInst&>(*this));
    case Type::LoadSlot:
        return std::make_unique<LoadSlotInst>(static_cast<const LoadSlotInst&>(*this));
    case Type::StoreSlot:
        return std::make_unique<StoreSlotInst>(static_cast<const StoreSlotInst&>(*this));
    }
    throw std::logic_error("无法复制未知的指令类型");
}

Effect Instruction::effect() const noexcept {
    switch (type_) {
    case Type::Phi:
    case Type::Parameter:
    case Type::Copy:
    case Type::Const:
    case Type::CellConstruct:
    case Type::MagicColon:
    case Type::Goto:
    case Type::Branch:
    case Type::Return:
    case Type::LoadSlot:
    case Type::Intrinsic:
        return Effect::NoSlotWrite;

    case Type::Concat:
    case Type::Colon:
    case Type::MagicEnd:
    case Type::GlobalDecl:
    case Type::PersistentDecl:
    case Type::Apply:
    case Type::ValueApply:
    case Type::Call:
    case Type::ParenReference:
    case Type::BraceReference:
    case Type::DotAccess:
    case Type::DotReference:
    case Type::ParenAssign:
    case Type::ParenDelete:
    case Type::BraceAssign:
    case Type::DotAssign:
    case Type::Unary:
    case Type::Binary:
    case Type::StoreSlot:
    // 屏障使工作区绑定失效；支持它的变换须按指令类型单独处理。
    case Type::WorkspaceBarrier:
        return Effect::MayWriteSlot;
    }
    return Effect::MayWriteSlot;
}

} // baltam 命名空间
