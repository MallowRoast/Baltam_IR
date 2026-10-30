#include "ir/ir_io.h"
#include "ir/ir_constant.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace baltam {
namespace {

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == text.npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r");
    return std::string(text.substr(first, last - first + 1));
}

bool begins(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

std::vector<std::string> split(std::string_view text, char separator = ',') {
    std::vector<std::string> result;
    if (trim(text).empty())
        return result;
    std::size_t begin = 0;
    do {
        const auto end = text.find(separator, begin);
        result.push_back(trim(text.substr(begin, end == text.npos ? end : end - begin)));
        if (end == text.npos)
            break;
        begin = end + 1;
    } while (true);
    return result;
}

std::uint32_t number(std::string_view text) {
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::invalid_argument("编号格式无效或超出范围：" + std::string(text));
    return value;
}

ValueId value_id(std::string_view text) {
    if (!begins(text, "%"))
        throw std::invalid_argument("缺少值编号：" + std::string(text));
    return ValueId{number(text.substr(1))};
}

SlotId slot_id(std::string_view text) {
    if (!begins(text, "slot"))
        throw std::invalid_argument("缺少槽位编号：" + std::string(text));
    return SlotId{number(text.substr(4))};
}

std::string unquote_path(std::string_view text) {
    if (text.size() < 2 || text.front() != '\'' || text.back() != '\'')
        throw std::invalid_argument("模块路径必须使用单引号");
    std::string result;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        char ch = text[i];
        if (ch == '\\') {
            if (++i + 1 >= text.size())
                throw std::invalid_argument("模块路径转义不完整");
            switch (text[i]) {
            case '\\':
                ch = '\\';
                break;
            case '\'':
                ch = '\'';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            default:
                throw std::invalid_argument("模块路径包含未知转义");
            }
        } else if (ch == '\'')
            throw std::invalid_argument("模块路径中的引号需要转义");
        result += ch;
    }
    return result;
}

struct Line {
    std::string text;
    std::size_t number;
};

struct UnitText {
    std::string header;
    std::vector<Line> body;
    std::string nested_parent;
};

struct PendingCallTarget {
    CallInst* instruction;
    std::string path;
};

class UnitReader {
public:
    UnitReader(IRUnit& unit, IRStageVersion& version, const UnitText& text,
               std::vector<PendingCallTarget>& pending_targets, const ConstantDecoder& constants)
        : unit_(unit), version_(version), text_(text), pending_targets_(pending_targets),
          constants_(constants) {}

    void read() {
        // 首先创建全部基本块，允许控制流前向引用。
        for (const auto& line : text_.body) {
            if (!line.text.empty() && line.text.back() == ':' && line.text.front() != ';') {
                const auto label = line.text.substr(0, line.text.size() - 1);
                if (blocks_.count(label))
                    throw std::invalid_argument("IR 第 " + std::to_string(line.number) +
                                                " 行：基本块标签重复");
                blocks_[label] = &version_.add_block(unit_, label);
            }
        }
        for (const auto& line : text_.body) {
            try {
                if (line.text.empty() || begins(line.text, ";"))
                    continue;
                if (begins(line.text, "stage ")) {
                    if (stage_seen_)
                        throw std::invalid_argument("重复的 IR 阶段声明");
                    stage_seen_ = true;
                    if (line.text != "stage Non-SSA" && line.text != "stage Based-SSA" &&
                        line.text != "stage Optimized-SSA")
                        throw std::invalid_argument("未知的 IR 阶段");
                    continue;
                }
                if (begins(line.text, "slot ")) {
                    read_slot(line);
                    continue;
                }

                if (line.text.back() == ':') {
                    block_ = blocks_.at(line.text.substr(0, line.text.size() - 1));
                    continue;
                }
                if (!block_)
                    throw std::invalid_argument("指令必须位于基本块中");
                read_instruction(line);
            } catch (const std::exception& error) {
                throw std::invalid_argument("IR 第 " + std::to_string(line.number) + " 行：" +
                                            error.what());
            }
        }
        if (!definitions_.empty() &&
            static_cast<std::uint64_t>(definitions_.rbegin()->first) + 1 != definitions_.size())
            throw std::invalid_argument("IR 值定义编号不连续");
        for (const auto id : references_)
            if (!definitions_.count(id))
                throw std::invalid_argument("IR 引用了未定义的值 %" + std::to_string(id));
        const auto constant = [&](ValueId id) { return definitions_.at(id.index).is_constant; };
        // 基本块打印顺序可能与 ValueId 分配顺序不同，全部定义收集后再推导常量性。
        for (std::size_t pass = 0; pass < definitions_.size(); ++pass) {
            bool changed = false;
            for (auto& [id, info] : definitions_) {
                bool known = info.is_constant;
                switch (info.def->type()) {
                case Instruction::Type::Concat: {
                    auto& inst = static_cast<ConcatInst&>(*info.def);
                    known = std::all_of(inst.items.begin(), inst.items.end(),
                                        [&](ValueId item) { return constant(item); });
                    inst.is_dynamic = !known;
                    break;
                }
                case Instruction::Type::Colon: {
                    const auto& inst = static_cast<const ColonInst&>(*info.def);
                    known = constant(inst.start) && (!inst.step || constant(*inst.step)) &&
                            constant(inst.stop);
                    break;
                }
                case Instruction::Type::Unary:
                    known = constant(static_cast<const UnaryInst&>(*info.def).operand);
                    break;
                case Instruction::Type::Binary: {
                    const auto& inst = static_cast<const BinaryInst&>(*info.def);
                    known = constant(inst.lhs) && constant(inst.rhs);
                    break;
                }
                default:
                    break;
                }
                if (known != info.is_constant) {
                    info.is_constant = known;
                    changed = true;
                }
            }
            if (!changed)
                break;
        }
        for (const auto& [id, info] : definitions_)
            (void)version_.add_value(info);
    }

private:
    IRUnit& unit_;
    IRStageVersion& version_;
    const UnitText& text_;
    std::vector<PendingCallTarget>& pending_targets_;
    bool stage_seen_ = false;
    std::map<std::string, BasicBlock*> blocks_;
    const ConstantDecoder& constants_;
    std::map<std::uint32_t, ValueInfo> definitions_;
    std::vector<std::uint32_t> references_;
    BasicBlock* block_ = nullptr;

    void read_slot(const Line& line) {
        const auto equal = line.text.find(" = ");
        if (equal == std::string::npos)
            throw std::invalid_argument("槽位声明缺少名称");
        const auto id = slot_id(line.text.substr(5, equal - 5));
        const auto name = line.text.substr(equal + 3);
        if (name.empty())
            throw std::invalid_argument("槽位名称为空");
        if (id.index < unit_.slot_table().size()) {
            if (unit_.slot_table().find(id).name != name)
                throw std::invalid_argument("槽位声明与函数签名冲突");
        } else if (id.index == unit_.slot_table().size()) {
            (void)unit_.slot_table().add_slot(name);
        } else
            throw std::invalid_argument("槽位编号不连续");
    }

    ValueId use(std::string_view text) {
        const auto id = value_id(text);
        references_.push_back(id.index);
        return id;
    }

    std::vector<ValueId> uses(std::string_view text) {
        std::vector<ValueId> ids;
        for (const auto& part : split(text))
            ids.push_back(use(part));
        return ids;
    }

    SlotId slot(std::string_view text) const {
        const auto id = slot_id(text);
        if (id.index >= unit_.slot_table().size())
            throw std::invalid_argument("引用了未定义的槽位：" + std::string(text));
        return id;
    }

    BasicBlock* target(std::string_view name) const {
        const auto it = blocks_.find(std::string(name));
        if (it == blocks_.end())
            throw std::invalid_argument("未知的基本块标签：" + std::string(name));
        return it->second;
    }

    void define(ValueId id, Instruction& inst, ValueInfo::Form form = ValueInfo::Form::Single,
                bool constant = false, std::uint32_t result_index = 0) {
        if (!definitions_.emplace(id.index, ValueInfo{&inst, result_index, form, constant}).second)
            throw std::invalid_argument("值定义编号重复");
    }

    void read_instruction(const Line& line) {
        std::string rhs = line.text;
        std::optional<ValueId> result;
        std::vector<ValueId> result_list;
        const auto equal = rhs.find(" = ");
        if (equal != std::string::npos) {
            const auto lhs = rhs.substr(0, equal);
            if (begins(lhs, "[") && lhs.back() == ']') {
                for (const auto& part : split(std::string_view(lhs).substr(1, lhs.size() - 2)))
                    result_list.push_back(value_id(part));
                if (result_list.size() < 2)
                    throw std::invalid_argument("多结果列表至少需要两个值");
            } else
                result = value_id(lhs);
            rhs.erase(0, equal + 3);
        }
        auto require_result = [&]() -> ValueId {
            if (!result || !result_list.empty())
                throw std::invalid_argument("指令需要单个结果编号");
            return *result;
        };
        auto no_result = [&]() {
            if (result || !result_list.empty())
                throw std::invalid_argument("该指令不应定义结果");
        };
        if (begins(rhs, "const ")) {
            auto decoded = constants_.decode(line.text);
            auto& inst = block_->add_instruction<ConstInst>(decoded->value, require_result());
            define(*result, inst, ValueInfo::Form::Single, true);
        } else if (begins(rhs, "load ")) {
            auto& inst =
                block_->add_instruction<LoadSlotInst>(require_result(), slot(rhs.substr(5)));
            define(*result, inst);
        } else if (begins(rhs, "store ")) {
            no_result();
            const auto parts = split(rhs.substr(6), ' ');
            if (parts.size() != 2)
                throw std::invalid_argument("存储指令的操作数无效");
            block_->add_instruction<StoreSlotInst>(slot(parts[0]), use(parts[1]));
        } else if (begins(rhs, "horzcat ") || begins(rhs, "vertcat ")) {
            const bool horizontal = begins(rhs, "horzcat ");
            auto items = uses(rhs.substr(8));
            if (items.empty())
                throw std::invalid_argument("并置输入为空");
            auto& inst = block_->add_instruction<ConcatInst>(
                std::move(items), require_result(),
                horizontal ? ConcatInst::Axis::Horizontal : ConcatInst::Axis::Vertical, true);
            define(*result, inst);
        } else if (begins(rhs, "cell ")) {
            const auto colon = rhs.find(" : ");
            const auto shape = rhs.substr(colon == std::string::npos ? rhs.size() : colon + 3);
            const auto x = shape.find('x');
            if (colon == std::string::npos || x == std::string::npos ||
                (shape.find(' ', x) == std::string::npos ||
                 shape.substr(shape.find(' ', x)) != " cell array"))
                throw std::invalid_argument("元胞构造指令的形状无效");
            auto items = uses(rhs.substr(5, colon - 5));
            auto& inst = block_->add_instruction<CellConstructInst>(
                std::move(items), number(shape.substr(0, x)),
                number(shape.substr(x + 1, shape.find(' ', x) - x - 1)), require_result());
            define(*result, inst);
        } else if (begins(rhs, "colon ")) {
            const auto parts = uses(rhs.substr(6));
            if (parts.size() != 2 && parts.size() != 3)
                throw std::invalid_argument("冒号指令的操作数无效");
            const auto step = parts.size() == 3 ? std::optional<ValueId>{parts[1]} : std::nullopt;
            auto& inst =
                block_->add_instruction<ColonInst>(require_result(), parts[0], step, parts.back());
            define(*result, inst);
        } else if (begins(rhs, "global ") || begins(rhs, "persistent ")) {
            no_result();
            const bool global = begins(rhs, "global ");
            std::vector<SlotId> ids;
            for (const auto& part : split(rhs.substr(global ? 7 : 11)))
                ids.push_back(slot(part));
            if (global)
                block_->add_instruction<GlobalDeclInst>(std::move(ids));
            else
                block_->add_instruction<PersistentDeclInst>(std::move(ids));
        } else if (begins(rhs, "goto ")) {
            no_result();
            block_->add_instruction<GotoInst>(target(rhs.substr(5)));
        } else if (begins(rhs, "branch ")) {
            no_result();
            const auto parts = split(rhs.substr(7));
            if (parts.size() != 3)
                throw std::invalid_argument("条件分支指令的操作数无效");
            block_->add_instruction<BranchInst>(use(parts[0]), target(parts[1]), target(parts[2]));
        } else if (rhs == "ret" || begins(rhs, "ret ")) {
            no_result();
            block_->add_instruction<ReturnInst>(uses(rhs.substr(3)));
        } else if (begins(rhs, "intrinsic.")) {
            const auto end = rhs.find(' ');
            const auto name = rhs.substr(10, end == std::string::npos ? end : end - 10);
            static const std::map<std::string, IntrinsicId> ids{
                {"switch_match", IntrinsicId::SwitchMatch},
                {"foreach_init", IntrinsicId::ForeachInit},
                {"foreach_count", IntrinsicId::ForeachCount},
                {"foreach_get", IntrinsicId::ForeachGet},
                {"int_gt", IntrinsicId::IntGt},
                {"int_add", IntrinsicId::IntAdd},
                {"logical_scalar", IntrinsicId::LogicalScalar}};
            const auto id = ids.find(name);
            if (id == ids.end())
                throw std::invalid_argument("未知的内部操作：" + name);
            auto& inst = block_->add_instruction<IntrinsicInst>(
                require_result(), id->second,
                end == std::string::npos ? std::vector<ValueId>{} : uses(rhs.substr(end + 1)));
            define(*result, inst);
        } else if (begins(rhs, "apply ") || begins(rhs, "call ") || begins(rhs, "value_apply ")) {
            const bool is_call = begins(rhs, "call ");
            DispatchType dispatch = DispatchType::Dynamic;
            std::string static_target;
            const auto attributes = rhs.find(" [dispatch=");
            if (attributes != std::string::npos) {
                if (!is_call || rhs.back() != ']')
                    throw std::invalid_argument("调用分派标记格式无效");
                const auto fields = split(
                    std::string_view(rhs).substr(attributes + 2, rhs.size() - attributes - 3));
                if (fields.empty() || !begins(fields[0], "dispatch="))
                    throw std::invalid_argument("调用分派类型缺失");
                static const std::map<std::string, DispatchType> types{
                    {"dynamic", DispatchType::Dynamic},
                    {"mfunction", DispatchType::MFunction},
                    {"builtin", DispatchType::Builtin},
                    {"internal", DispatchType::Internal},
                    {"plugin", DispatchType::Plugin}};
                const auto type = types.find(fields[0].substr(9));
                if (type == types.end())
                    throw std::invalid_argument("未知的调用分派类型：" + fields[0].substr(9));
                dispatch = type->second;
                if (fields.size() > 2 || (fields.size() == 2 && !begins(fields[1], "target=@")))
                    throw std::invalid_argument("调用目标标记格式无效");
                if (fields.size() == 2) {
                    static_target = fields[1].substr(8);
                    if (static_target.empty())
                        throw std::invalid_argument("静态调用目标路径为空");
                }
                rhs.erase(attributes);
            }
            const auto space = rhs.find(' '), open = rhs.find('(', space + 1);
            if (open == std::string::npos || rhs.back() != ')')
                throw std::invalid_argument("调用格式无效");
            const auto callee = rhs.substr(space + 1, open - space - 1);
            auto args = uses(rhs.substr(open + 1, rhs.size() - open - 2));
            std::vector<ValueId> results = std::move(result_list);
            if (result)
                results.push_back(*result);
            auto define_all = [&](Instruction& inst) {
                for (std::size_t i = 0; i < results.size(); ++i)
                    define(results[i], inst, ValueInfo::Form::Single, false,
                           static_cast<std::uint32_t>(i));
            };
            if (is_call) {
                auto& inst =
                    block_->add_instruction<CallInst>(results, callee, std::move(args), dispatch);
                define_all(inst);
                if (!static_target.empty())
                    pending_targets_.push_back({&inst, static_target});
            } else if (begins(rhs, "apply ")) {
                Operand base = begins(callee, "%") ? Operand{use(callee)} : Operand{callee};
                auto& inst =
                    block_->add_instruction<ApplyInst>(results, std::move(base), std::move(args));
                define_all(inst);
            } else {
                auto& inst =
                    block_->add_instruction<ValueApplyInst>(results, std::move(args), use(callee));
                define_all(inst);
            }
        } else if (begins(rhs, "paren_ref ") || begins(rhs, "brace_ref ") ||
                   begins(rhs, "paren_assign ") || begins(rhs, "brace_assign ") ||
                   begins(rhs, "paren_delete ")) {
            const auto space = rhs.find(' ');
            const auto op = rhs.substr(0, space);
            const auto args = uses(rhs.substr(space + 1));
            if (args.empty())
                throw std::invalid_argument("索引指令缺少基值");
            if (op == "paren_ref" || op == "brace_ref") {
                if (args.size() < 2)
                    throw std::invalid_argument("索引指令缺少下标");
                std::vector<ValueId> indices(args.begin() + 1, args.end());
                if (op == "paren_ref") {
                    auto& inst = block_->add_instruction<ParenReferenceInst>(
                        require_result(), std::move(indices), args[0]);
                    define(*result, inst);
                } else {
                    auto& inst = block_->add_instruction<BraceReferenceInst>(
                        require_result(), std::move(indices), args[0]);
                    define(*result, inst, ValueInfo::Form::MaybePack);
                }
            } else {
                if (args.size() < (op == "paren_delete" ? 2U : 3U))
                    throw std::invalid_argument("赋值索引不足");
                const bool deletion = op == "paren_delete";
                std::vector<ValueId> indices(args.begin() + 1, args.end() - (deletion ? 0 : 1));
                if (deletion) {
                    auto& inst = block_->add_instruction<ParenDeleteInst>(
                        std::move(indices), require_result(), args[0]);
                    define(*result, inst);
                } else if (op == "paren_assign") {
                    auto& inst = block_->add_instruction<ParenAssignInst>(
                        std::move(indices), require_result(), args[0], args.back());
                    define(*result, inst);
                } else {
                    auto& inst = block_->add_instruction<BraceAssignInst>(
                        std::move(indices), args.back(), require_result(), args[0]);
                    define(*result, inst);
                }
            }
        } else if (begins(rhs, "dot_access ") || begins(rhs, "dot_ref ") ||
                   begins(rhs, "dot_assign ")) {
            const auto space = rhs.find(' ');
            const auto op = rhs.substr(0, space);
            std::string object = rhs.substr(space + 1);
            std::optional<ValueId> assigned;
            if (op == "dot_assign") {
                const auto comma = object.rfind(", %");
                if (comma == std::string::npos)
                    throw std::invalid_argument("字段赋值指令缺少赋值数据");
                assigned = use(object.substr(comma + 2));
                object.erase(comma);
            }
            const auto dynamic = object.find(".(%");
            const auto dot = dynamic == std::string::npos ? object.rfind('.') : dynamic;
            if (dot == std::string::npos || dot == 0 || dot + 1 == object.size())
                throw std::invalid_argument("点号成员格式无效");
            const auto base_text = object.substr(0, dot);
            Operand base = begins(base_text, "%") ? Operand{use(base_text)} : Operand{base_text};
            Operand member;
            if (dynamic != std::string::npos) {
                if (object.back() != ')')
                    throw std::invalid_argument("动态字段名称缺少右括号");
                member = use(object.substr(dot + 2, object.size() - dot - 3));
            } else {
                member = object.substr(dot + 1);
            }
            if (op == "dot_access") {
                auto& inst = block_->add_instruction<DotAccessInst>(require_result(), base, member);
                define(*result, inst, ValueInfo::Form::MaybePack);
            } else if (op == "dot_ref") {
                if (!std::holds_alternative<ValueId>(base))
                    throw std::invalid_argument("字段引用的基值必须是值编号");
                auto& inst = block_->add_instruction<DotReferenceInst>(require_result(), member,
                                                                       std::get<ValueId>(base));
                define(*result, inst, ValueInfo::Form::MaybePack);
            } else {
                if (!std::holds_alternative<ValueId>(base))
                    throw std::invalid_argument("字段赋值的基值必须是值编号");
                auto& inst = block_->add_instruction<DotAssignInst>(
                    member, require_result(), std::get<ValueId>(base), *assigned);
                define(*result, inst);
            }
        } else {
            static const std::map<std::string, UnaryOp> unary{{"uplus", UnaryOp::Uplus},
                                                              {"uminus", UnaryOp::Uminus},
                                                              {"not", UnaryOp::LogicalNot},
                                                              {"transpose", UnaryOp::Transpose},
                                                              {"ctranspose", UnaryOp::Ctranspose}};
            static const std::map<std::string, BinaryOp> binary{{"plus", BinaryOp::Add},
                                                                {"minus", BinaryOp::Sub},
                                                                {"mtimes", BinaryOp::Mul},
                                                                {"mrdivide", BinaryOp::Rdiv},
                                                                {"mldivide", BinaryOp::Ldiv},
                                                                {"mpower", BinaryOp::Pow},
                                                                {"times", BinaryOp::ElemMul},
                                                                {"rdivide", BinaryOp::ElemRdiv},
                                                                {"ldivide", BinaryOp::ElemLdiv},
                                                                {"power", BinaryOp::ElemPow},
                                                                {"and", BinaryOp::And},
                                                                {"or", BinaryOp::Or},
                                                                {"lt", BinaryOp::Lt},
                                                                {"le", BinaryOp::Le},
                                                                {"gt", BinaryOp::Gt},
                                                                {"ge", BinaryOp::Ge},
                                                                {"eq", BinaryOp::Eq},
                                                                {"ne", BinaryOp::Ne}};
            const auto space = rhs.find(' ');
            const auto name = rhs.substr(0, space);
            if (unary.count(name)) {
                auto arg = use(rhs.substr(space + 1));
                auto& inst =
                    block_->add_instruction<UnaryInst>(require_result(), arg, unary.at(name));
                define(*result, inst);
            } else if (binary.count(name)) {
                const auto args = uses(rhs.substr(space + 1));
                if (args.size() != 2)
                    throw std::invalid_argument("二元运算操作数无效");
                auto& inst = block_->add_instruction<BinaryInst>(require_result(), args[0], args[1],
                                                                 binary.at(name));
                define(*result, inst);
            } else
                throw std::invalid_argument("未知或尚未支持的指令：" + rhs);
        }
    }
};

std::vector<std::string> signature(std::string_view text, std::vector<SlotId>& ids) {
    std::vector<std::string> names;
    for (const auto& part : split(text)) {
        const auto space = part.find(' ');
        if (space == std::string::npos || space + 1 == part.size())
            throw std::invalid_argument("函数签名无效");
        ids.push_back(slot_id(part.substr(0, space)));
        names.push_back(part.substr(space + 1));
    }
    return names;
}

std::unique_ptr<FunctionUnit> read_function(const UnitText& text,
                                            std::vector<PendingCallTarget>& pending_targets,
                                            const ConstantDecoder& constants) {
    const auto open = text.header.find('('), close = text.header.find(')', open);
    if (!begins(text.header, "define @") || open == std::string::npos ||
        close == std::string::npos || text.header.back() != '{')
        throw std::invalid_argument("函数定义格式无效");
    const auto name = text.header.substr(8, open - 8);
    std::vector<SlotId> param_ids, return_ids;
    auto params = signature(text.header.substr(open + 1, close - open - 1), param_ids);
    std::vector<std::string> returns;
    const auto tail = trim(std::string_view(text.header).substr(close + 1));
    if (tail != "{") {
        if (!begins(tail, "-> (") || tail.size() < 6 || tail.substr(tail.size() - 3) != ") {")
            throw std::invalid_argument("函数返回值签名无效");
        returns = signature(tail.substr(4, tail.size() - 7), return_ids);
    }
    auto function = std::make_unique<FunctionUnit>(name, std::move(params), std::move(returns));
    if (function->parameter_slots() != param_ids || function->return_slots() != return_ids)
        throw std::invalid_argument("函数签名的槽位编号不一致");
    IRStage stage = IRStage::NonSSA;
    for (const auto& line : text.body) {
        if (line.text == "stage Based-SSA")
            stage = IRStage::SSA;
        if (line.text == "stage Optimized-SSA")
            stage = IRStage::OptimizedSSA;
    }
    function->set_current_stage(stage);
    UnitReader(*function, *function->stage_version(stage), text, pending_targets, constants).read();
    return function;
}

FunctionUnit* function_at_path(
    const std::vector<std::pair<std::string, std::unique_ptr<FunctionUnit>>>& functions,
    std::string_view path) {
    const auto parts = split(path, '.');
    if (parts.empty())
        throw std::invalid_argument("函数路径为空");
    const auto index = number(parts.front());
    if (index >= functions.size())
        throw std::invalid_argument("顶层函数路径不存在：" + std::string(path));
    auto* function = functions[index].second.get();
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto child = number(parts[i]);
        if (child >= function->nested_functions().size())
            throw std::invalid_argument("嵌套函数路径不存在：" + std::string(path));
        function = function->nested_functions()[child].get();
    }
    return function;
}

} // 匿名命名空间

IRPrinter::IRPrinter(std::filesystem::path resource_directory,
                     std::filesystem::path constant_directory)
    : resource_directory_(std::move(resource_directory)),
      constant_directory_(std::move(constant_directory)),
      constants_(resource_directory_, constant_directory_) {}

std::string IRPrinter::print(const IRModule& module) {
    // 标签仅属于本次输出，重复打印或上一次输出失败都不应留下旧状态。
    constants_ = ConstantEncoder(resource_directory_, constant_directory_);
    std::ostringstream output;
    if (module.source_path()) {
        std::string escaped;
        for (char c : module.source_path()->string()) {
            if (c == '\\' || c == '\'')
                escaped += '\\';
            if (c == '\n')
                escaped += "\\n";
            else if (c == '\r')
                escaped += "\\r";
            else if (c == '\t')
                escaped += "\\t";
            else
                escaped += c;
        }
        output << "; ModulePath = '" << escaped << "'\n\n";
    }
    if (const auto* command = module.command_unit()) {
        output << "script {\n";
        print_slots(output, *command);
        print_blocks(output, *command->stage_version());
        output << "}\n";
    }
    for (std::size_t i = 0; i < module.functions().size(); ++i) {
        if (module.command_unit() || i != 0)
            output << '\n';
        print_function(output, *module.functions()[i], std::to_string(i));
    }
    if (module.command_unit() || !module.functions().empty())
        output << '\n';
    output << constants_.metadata();
    return output.str();
}

void IRPrinter::print_slots(std::ostream& output, const IRUnit& unit) const {
    if (unit.slot_table().size() == 0)
        return;
    output << "; slots:\n";
    for (std::size_t i = 0; i < unit.slot_table().size(); ++i) {
        output << ";   slot" << i << " = "
               << unit.slot_table().find(SlotId{static_cast<std::uint32_t>(i)}).name << '\n';
    }
    output << '\n';
}

void IRPrinter::print_blocks(std::ostream& output, const IRStageVersion& version) {
    for (const auto& block : version.blocks()) {
        output << block->label() << ":\n";
        for (const auto& instruction : block->instructions()) {
            output << "    ";
            if (instruction->type() == Instruction::Type::Const)
                output << constants_.encode(static_cast<const ConstInst&>(*instruction));
            else
                output << instruction->to_string();
            output << '\n';
        }
    }
}

void IRPrinter::print_function(std::ostream& output, const FunctionUnit& function,
                               const std::string& path) {
    const auto signature = [&](const std::vector<SlotId>& slots) {
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (i)
                output << ", ";
            output << "slot" << slots[i].index << ' ' << function.slot_table().find(slots[i]).name;
        }
    };
    output << "define @" << function.name() << '(';
    signature(function.parameter_slots());
    output << ')';
    if (!function.return_slots().empty()) {
        output << " -> (";
        signature(function.return_slots());
        output << ')';
    }
    const char* stage;
    switch (function.current_stage()) {
    case IRStage::NonSSA:
        stage = "Non-SSA";
        break;
    case IRStage::SSA:
        stage = "Based-SSA";
        break;
    case IRStage::OptimizedSSA:
        stage = "Optimized-SSA";
        break;
    default:
        throw std::invalid_argument("无效的 IR 阶段");
    }
    output << " {\n; stage: " << stage << '\n';
    print_slots(output, function);
    print_blocks(output, *function.current_version());
    output << "}\n";
    for (std::size_t i = 0; i < function.nested_functions().size(); ++i) {
        output << "\nnested in @" << path << "\n; nested in @" << function.name() << '\n';
        print_function(output, *function.nested_functions()[i], path + "." + std::to_string(i));
    }
}

std::unique_ptr<IRModule> read_ir(std::string_view text,
                                  const std::filesystem::path& resource_directory) {
    std::vector<Line> lines;
    std::istringstream input{std::string(text)};
    std::string raw;
    std::string metadata;
    bool in_metadata = false;
    std::vector<std::string> constant_lines;
    for (std::size_t n = 1; std::getline(input, raw); ++n) {
        auto line = trim(raw);
        if (line == "constants {")
            in_metadata = true;
        if (in_metadata) {
            metadata += raw + "\n";
            continue;
        }
        if (line.find(" = const ") != std::string::npos && begins(line, "%"))
            constant_lines.push_back(line);
        // 现有打印器通过这些注释记录阶段和槽位等结构信息。
        if (begins(line, "; stage: "))
            line = "stage " + line.substr(9);
        if (begins(line, ";   slot"))
            line = "slot " + line.substr(4);
        lines.push_back({line, n});
    }
    if (metadata.empty())
        throw std::invalid_argument("IR 缺少常量表");
    ConstantDecoder constants(metadata, constant_lines, resource_directory);
    std::optional<std::filesystem::path> path;
    std::vector<UnitText> units;
    std::string pending_parent;
    for (std::size_t i = 0; i < lines.size();) {
        const auto& line = lines[i];
        if (begins(line.text, "; ModulePath = ")) {
            if (path)
                throw std::invalid_argument("重复的模块路径");
            path = unquote_path(std::string_view(line.text).substr(15));
            ++i;
            continue;
        }
        if (line.text.empty() || begins(line.text, ";")) {
            ++i;
            continue;
        }
        if (begins(line.text, "module_path ")) {
            if (path)
                throw std::invalid_argument("重复的模块路径");
            path = unquote_path(std::string_view(line.text).substr(12));
            ++i;
            continue;
        }
        if (begins(line.text, "nested in @")) {
            if (!pending_parent.empty())
                throw std::invalid_argument("重复的嵌套函数关系");
            pending_parent = line.text.substr(11);
            ++i;
            continue;
        }
        if (line.text == "script {" || begins(line.text, "define @")) {
            if (line.text == "script {" && !pending_parent.empty())
                throw std::invalid_argument("脚本不能声明嵌套函数关系");
            UnitText unit{line.text, {}, pending_parent};
            pending_parent.clear();
            ++i;
            while (i < lines.size() && lines[i].text != "}")
                unit.body.push_back(lines[i++]);
            if (i == lines.size())
                throw std::invalid_argument("IR 单元缺少右花括号");
            units.push_back(std::move(unit));
            ++i;
            continue;
        }
        throw std::invalid_argument("IR 第 " + std::to_string(line.number) +
                                    " 行：未知的模块级文本");
    }
    if (units.empty() || !pending_parent.empty())
        throw std::invalid_argument("IR 文件没有完整的代码单元");
    const bool script = !units.empty() && units.front().header == "script {";
    // IR 文件读取仅用于观察，不注册到 M 源文件缓存。
    auto module = std::make_unique<IRModule>(
        path ? SourceType::File : SourceType::Memory,
        script ? ModuleType::Script : ModuleType::FunctionFile, path);
    std::vector<std::pair<std::string, std::unique_ptr<FunctionUnit>>> functions;
    std::vector<PendingCallTarget> pending_targets;
    for (const auto& unit : units) {
        if (unit.header == "script {") {
            if (module->command_unit() || !functions.empty())
                throw std::invalid_argument("脚本单元顺序无效");
            auto command = std::make_unique<CommandUnit>();
            UnitReader(*command, *command->stage_version(), unit, pending_targets, constants)
                .read();
            module->set_command_unit(std::move(command));
        } else {
            auto function = read_function(unit, pending_targets, constants);
            if (unit.nested_parent.empty())
                functions.emplace_back(function->name(), std::move(function));
            else {
                function_at_path(functions, unit.nested_parent)
                    ->append_nested_function(std::move(function));
            }
        }
    }
    for (const auto& target : pending_targets)
        target.instruction->m_function_target = function_at_path(functions, target.path);
    module->set_functions(std::move(functions));
    return module;
}

std::unique_ptr<IRModule> read_ir_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("无法打开 IR 文件：" + path.string());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad())
        throw std::runtime_error("读取 IR 文件失败：" + path.string());
    return read_ir(buffer.str(), std::filesystem::absolute(path).parent_path());
}

void write_ir_file(const IRModule& module, const std::filesystem::path& path) {
    const auto absolute = std::filesystem::absolute(path);
    const auto text =
        IRPrinter(absolute.parent_path(), path.filename().string() + ".constants").print(module);
    std::ofstream output(absolute, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("无法打开 IR 输出文件：" + path.string());
    output << text;
    output.close();
    if (!output)
        throw std::runtime_error("写入 IR 文件失败：" + path.string());
}

} // baltam 命名空间
