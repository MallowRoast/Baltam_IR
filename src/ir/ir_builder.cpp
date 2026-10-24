#include "ir/ir_builder.h"
#include "ir/ir_cache.h"

#include "ba_obj/ba_obj.h"
#include "ba_obj/cell.h"
#include "bt_ast_interface/bt_ast_interface.h"
#include "bt_ast_interface/ast/numval.h"
#include "bt_ast_interface/ast/symref.h"
#include "bt_ast_interface/ast/text_node.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace baltam {

ValueId IRBuilder::add_result(
    Instruction& instruction, ValueId& result,
    ValueInfo::Form form, bool is_constant) {
    result = version_.add_value(ValueInfo{&instruction, 0, form, is_constant});
    return result;
}

ValueId IRBuilder::create_const(const_ba_obj_ptr value) {
    auto& instruction = block_->add_instruction<ConstInst>(std::move(value), ValueId{0});
    return add_result(instruction, instruction.result, ValueInfo::Form::Single, true);
}

ValueId IRBuilder::create_load(SlotId slot) {
    auto& instruction = block_->add_instruction<LoadSlotInst>(ValueId{0}, slot);
    return add_result(instruction, instruction.result);
}

ValueId IRBuilder::create_concat(std::vector<ValueId> items, ConcatInst::Axis axis) {
    if (items.empty()) throw std::invalid_argument("并置输入不能为空");
    if (items.size() == 1) return items.front();
    bool is_dynamic = false;
    for (ValueId item : items) {
        is_dynamic |= !version_.values().find(item).is_constant;
    }
    auto& instruction = block_->add_instruction<ConcatInst>(
        std::move(items), ValueId{0}, axis, is_dynamic);
    return add_result(instruction, instruction.result, ValueInfo::Form::Single, !is_dynamic);
}

ValueId IRBuilder::create_cell(std::vector<ValueId> items, std::size_t rows, std::size_t cols) {
    if ((cols && rows > std::numeric_limits<std::size_t>::max() / cols) ||
        items.size() != rows * cols)
        throw std::invalid_argument("元胞数组形状与元素数量不匹配");

    std::vector<const_ba_obj_ptr> constants;
    constants.reserve(items.size());
    bool foldable = true;
    for (ValueId item : items) {
        const auto& info = version_.values().find(item);
        if (!info.is_constant || !info.def ||
            info.def->type() != Instruction::Type::Const) {
            foldable = false;
            break;
        }
        constants.push_back(static_cast<const ConstInst&>(*info.def).value);
    }
    if (foldable) {
        auto cell = std::make_unique<cell_array>(rows, cols);
        for (std::size_t i = 0; i < constants.size(); ++i)
            (*cell)(i / cols, i % cols) = std::make_shared<ba_obj>(*constants[i]);
        return create_const(std::make_shared<ba_obj>(cell.release()));
    }

    auto& instruction = block_->add_instruction<CellConstructInst>(
        std::move(items), rows, cols, ValueId{0});
    return add_result(instruction, instruction.result);
}

ValueId IRBuilder::create_colon(ValueId start, std::optional<ValueId> step, ValueId stop) {
    const bool is_constant = version_.values().find(start).is_constant &&
                             (!step || version_.values().find(*step).is_constant) &&
                             version_.values().find(stop).is_constant;
    auto& instruction = block_->add_instruction<ColonInst>(
        ValueId{0}, start, step, stop);
    return add_result(instruction, instruction.result, ValueInfo::Form::Single, is_constant);
}

ValueId IRBuilder::create_unary(ValueId operand, UnaryOp op) {
    const bool is_constant = version_.values().find(operand).is_constant;
    auto& instruction = block_->add_instruction<UnaryInst>(
        ValueId{0}, operand, op);
    return add_result(instruction, instruction.result, ValueInfo::Form::Single, is_constant);
}

ValueId IRBuilder::create_binary(ValueId lhs, ValueId rhs, BinaryOp op) {
    const bool is_constant = version_.values().find(lhs).is_constant &&
                             version_.values().find(rhs).is_constant;
    auto& instruction = block_->add_instruction<BinaryInst>(
        ValueId{0}, lhs, rhs, op);
    return add_result(instruction, instruction.result, ValueInfo::Form::Single, is_constant);
}

ValueId IRBuilder::create_brace_reference(ValueId base, std::vector<ValueId> indices) {
    (void)version_.values().find(base);
    if (indices.empty()) throw std::invalid_argument("花括号索引不能为空");
    for (ValueId index : indices) (void)version_.values().find(index);
    auto& instruction = block_->add_instruction<BraceReferenceInst>(
        ValueId{0}, std::move(indices), base);
    return add_result(instruction, instruction.result, ValueInfo::Form::MaybePack);
}

ValueId IRBuilder::create_brace_assign(ValueId base, std::vector<ValueId> indices, ValueId value) {
    (void)version_.values().find(base);
    (void)version_.values().find(value);
    if (indices.empty()) throw std::invalid_argument("花括号索引不能为空");
    for (ValueId index : indices) (void)version_.values().find(index);
    auto& instruction = block_->add_instruction<BraceAssignInst>(
        std::move(indices), value, ValueId{0}, base);
    return add_result(instruction, instruction.result);
}

ValueId IRBuilder::create_apply(InternedString name, std::vector<ValueId> arguments) {
    for (ValueId argument : arguments) (void)version_.values().find(argument);
    auto& instruction = block_->add_instruction<ApplyInst>(
        std::vector<ValueId>{}, Operand{std::move(name)}, std::move(arguments));
    const auto result = version_.add_value(
        ValueInfo{&instruction, 0, ValueInfo::Form::Single, false});
    instruction.results.push_back(result);
    return result;
}

CallInst& IRBuilder::create_call(InternedString name, std::vector<ValueId> arguments) {
    for (ValueId argument : arguments) (void)version_.values().find(argument);
    return block_->add_instruction<CallInst>(
        std::vector<ValueId>{}, std::move(name), std::move(arguments));
}

ValueApplyInst& IRBuilder::create_value_apply(ValueId base, std::vector<ValueId> arguments) {
    (void)version_.values().find(base);
    for (ValueId argument : arguments) (void)version_.values().find(argument);
    return block_->add_instruction<ValueApplyInst>(
        std::vector<ValueId>{}, std::move(arguments), base);
}

ValueId IRBuilder::create_paren_reference(ValueId base, std::vector<ValueId> indices) {
    (void)version_.values().find(base);
    if (indices.empty()) throw std::invalid_argument("圆括号索引不能为空");
    for (ValueId index : indices) (void)version_.values().find(index);
    auto& instruction = block_->add_instruction<ParenReferenceInst>(
        ValueId{0}, std::move(indices), base);
    return add_result(instruction, instruction.result);
}

ValueId IRBuilder::create_paren_assign(ValueId base, std::vector<ValueId> indices, ValueId value) {
    (void)version_.values().find(base);
    (void)version_.values().find(value);
    if (indices.empty()) throw std::invalid_argument("圆括号索引不能为空");
    for (ValueId index : indices) (void)version_.values().find(index);
    auto& instruction = block_->add_instruction<ParenAssignInst>(
        std::move(indices), ValueId{0}, base, value);
    return add_result(instruction, instruction.result);
}

ValueId IRBuilder::create_paren_delete(ValueId base, std::vector<ValueId> indices) {
    (void)version_.values().find(base);
    if (indices.empty()) throw std::invalid_argument("圆括号索引不能为空");
    for (ValueId index : indices) (void)version_.values().find(index);
    auto& instruction = block_->add_instruction<ParenDeleteInst>(
        std::move(indices), ValueId{0}, base);
    return add_result(instruction, instruction.result);
}

ValueId IRBuilder::create_dot_access(InternedString base_name, Operand member) {
    if (const auto* value = std::get_if<ValueId>(&member))
        (void)version_.values().find(*value);
    auto& instruction = block_->add_instruction<DotAccessInst>(
        ValueId{0}, Operand{std::move(base_name)}, std::move(member));
    return add_result(instruction, instruction.result, ValueInfo::Form::MaybePack);
}

ValueId IRBuilder::create_dot_reference(ValueId base, Operand member) {
    (void)version_.values().find(base);
    if (const auto* value = std::get_if<ValueId>(&member))
        (void)version_.values().find(*value);
    auto& instruction = block_->add_instruction<DotReferenceInst>(
        ValueId{0}, std::move(member), base);
    return add_result(instruction, instruction.result, ValueInfo::Form::MaybePack);
}

ValueId IRBuilder::create_dot_assign(ValueId base, Operand member, ValueId value) {
    (void)version_.values().find(base);
    (void)version_.values().find(value);
    if (const auto* dynamic = std::get_if<ValueId>(&member))
        (void)version_.values().find(*dynamic);
    auto& instruction = block_->add_instruction<DotAssignInst>(
        std::move(member), ValueId{0}, base, value);
    return add_result(instruction, instruction.result);
}

GlobalDeclInst& IRBuilder::create_global_decl(std::vector<SlotId> slots) {
    if (slots.empty()) throw std::invalid_argument("全局变量声明不能为空");
    return block_->add_instruction<GlobalDeclInst>(std::move(slots));
}

PersistentDeclInst& IRBuilder::create_persistent_decl(std::vector<SlotId> slots) {
    if (slots.empty()) throw std::invalid_argument("持久变量声明不能为空");
    return block_->add_instruction<PersistentDeclInst>(std::move(slots));
}

BranchInst& IRBuilder::create_branch(ValueId condition, BasicBlock& true_target, BasicBlock& false_target) {
    (void)version_.values().find(condition);
    return block_->add_instruction<BranchInst>(
        condition, &true_target, &false_target);
}

GotoInst& IRBuilder::create_goto(BasicBlock& target) {
    return block_->add_instruction<GotoInst>(&target);
}

ValueId IRBuilder::create_switch_match(ValueId value, ValueId candidate) {
    return create_intrinsic(IntrinsicId::SwitchMatch, {value, candidate});
}

ValueId IRBuilder::create_intrinsic(IntrinsicId id, std::vector<ValueId> operands) {
    for (ValueId operand : operands) (void)version_.values().find(operand);
    auto& instruction = block_->add_instruction<IntrinsicInst>(
        ValueId{0}, id, std::move(operands));
    return add_result(instruction, instruction.result);
}

GotoInst& IRBuilder::create_break(const LoopControlStack& loops) {
    return create_goto(*loops.current().break_target);
}

GotoInst& IRBuilder::create_continue(const LoopControlStack& loops) {
    return create_goto(*loops.current().continue_target);
}

StoreSlotInst& IRBuilder::create_store(SlotId slot, ValueId value) {
    return block_->add_instruction<StoreSlotInst>(slot, value);
}

ReturnInst& IRBuilder::create_return(std::vector<ValueId> values) {
    return block_->add_instruction<ReturnInst>(std::move(values));
}

namespace {

const ast& child(const ast& node, std::size_t index) {
    const auto& value = node.branch.at(index);
    if (!value) throw std::invalid_argument("AST 子节点为空");
    return *value;
}

std::string name(const ast& node) {
    if (node.nodetype != node_name) throw std::invalid_argument("预期名字节点");
    return static_cast<const symref&>(node).name();
}

std::optional<UnaryOp> unary_op(nodeType type) {
    switch (type) {
        case node_uplus: return UnaryOp::Uplus;
        case node_negative: return UnaryOp::Uminus;
        case node_logic_not: return UnaryOp::LogicalNot;
        case node_transpose: return UnaryOp::Transpose;
        case node_ctranspose: return UnaryOp::Ctranspose;
        default: return std::nullopt;
    }
}

std::optional<BinaryOp> binary_op(nodeType type) {
    switch (type) {
        case node_add: return BinaryOp::Add;
        case node_subtract: return BinaryOp::Sub;
        case node_multiply: return BinaryOp::Mul;
        case node_right_divide: return BinaryOp::Rdiv;
        case node_left_divide: return BinaryOp::Ldiv;
        case node_power: return BinaryOp::Pow;
        case node_element_mul: return BinaryOp::ElemMul;
        case node_element_rdiv: return BinaryOp::ElemRdiv;
        case node_element_ldiv: return BinaryOp::ElemLdiv;
        case node_element_power: return BinaryOp::ElemPow;
        case node_logic_and: return BinaryOp::And;
        case node_logic_or: return BinaryOp::Or;
        case node_less_than: return BinaryOp::Lt;
        case node_leq: return BinaryOp::Le;
        case node_greater_than: return BinaryOp::Gt;
        case node_geq: return BinaryOp::Ge;
        case node_eq: return BinaryOp::Eq;
        case node_noteq: return BinaryOp::Ne;
        default: return std::nullopt;
    }
}

bool is_short_circuit(nodeType type) {
    return type == node_logic_and_short || type == node_logic_or_short;
}

std::vector<std::string> signature_names(const ast_ptr& node) {
    if (!node) return {};
    if (node->nodetype == node_name)
        return {static_cast<const symref&>(*node).name()};
    if (node->nodetype != node_list)
        throw std::invalid_argument("函数签名中的参数节点无效");
    std::vector<std::string> names;
    for (const auto& item : node->branch) {
        if (!item || item->nodetype != node_name)
            throw std::invalid_argument("函数签名中的参数名称无效");
        names.push_back(static_cast<const symref&>(*item).name());
    }
    return names;
}

std::unique_ptr<FunctionUnit> build_function_tree(const ast& root) {
    if (root.nodetype != node_mfile_func || root.branch.size() < 6)
        throw std::invalid_argument("预期函数 AST");
    auto function = std::make_unique<FunctionUnit>(
        name(child(root, 1)), signature_names(root.branch.at(2)),
        signature_names(root.branch.at(0)));
    IRBuilder(*function).build(root);
    // AST 第 5 个子节点保存直接嵌套的函数；递归结果由词法父函数拥有。
    if (const auto& nested = root.branch.at(5)) {
        if (nested->nodetype != node_list)
            throw std::invalid_argument("嵌套函数列表节点无效");
        for (const auto& item : nested->branch) {
            if (!item) throw std::invalid_argument("嵌套函数节点为空");
            function->append_nested_function(build_function_tree(*item));
        }
    }
    return function;
}

} // namespace

std::shared_ptr<IRModule> IRBuilder::build_file(
    const std::filesystem::path& input) {
    namespace fs = std::filesystem;
    const auto path = fs::absolute(input).lexically_normal();
    if (!fs::is_regular_file(path))
        throw std::runtime_error("文件不存在或不是普通文件: " + path.string());
    if (path.extension() != ".m")
        throw std::invalid_argument("预期 .m 文件: " + path.string());

    auto& cache = IRCache::instance();
    if (auto* entry = cache.find(path); entry && entry->is_valid())
        return entry->module;

    namespace parser = bt_ast_interface;
    const int initialized = parser::initialize(BT_AST_INIT_NO_BUILTIN);
    if (initialized != BT_AST_INIT_SUCCESS)
        throw std::runtime_error("AST 解析器初始化失败: " +
                                 std::to_string(initialized));

    std::shared_ptr<IRModule> module;
    try {
        // 解析器返回的工作区顺序不保证是源码顺序，先按起始位置恢复定义顺序。
        std::string message;
        auto workspaces = parser::parse_mfile(path.string(), ParserOpts{}, message);
        if (!message.empty()) throw std::runtime_error("解析失败: " + message);
        if (workspaces.empty())
            throw std::runtime_error("文件中没有可处理的代码单元");

        std::vector<std::size_t> order;
        order.reserve(workspaces.size());
        for (std::size_t i = 0; i < workspaces.size(); ++i) {
            if (!workspaces[i] || !workspaces[i]->ast)
                throw std::runtime_error("AST 工作区或根节点为空");
            if (!workspaces[i]->is_mfun() && !workspaces[i]->is_mscript())
                throw std::runtime_error("不支持的 .m 文件代码单元");
            order.push_back(i);
        }
        std::stable_sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
            const auto& left = workspaces[lhs]->ast->loc.begin;
            const auto& right = workspaces[rhs]->ast->loc.begin;
            return left.line == right.line ? left.column < right.column
                                           : left.line < right.line;
        });

        const auto script = workspaces[order.front()]->is_mscript();
        // 脚本主体单独进入 CommandUnit；函数文件的首个函数成为模块入口。
        module = std::make_shared<IRModule>(
            SourceType::File, script ? ModuleType::Script : ModuleType::FunctionFile, path);
        std::vector<std::pair<std::string, std::unique_ptr<FunctionUnit>>> functions;
        functions.reserve(order.size() - static_cast<std::size_t>(script));
        for (const auto i : order) {
            const auto& workspace = *workspaces[i];
            if (workspace.is_mscript()) {
                if (!script || i != order.front())
                    throw std::runtime_error("脚本工作区位置无效");
                auto command = std::make_unique<CommandUnit>();
                IRBuilder(*command).build(*workspace.ast);
                module->set_command_unit(std::move(command));
                continue;
            }
            const auto& root = *workspace.ast;
            functions.emplace_back(workspace.funname, build_function_tree(root));
        }
        // 所有代码单元构建成功后再发布函数列表和索引。
        module->set_functions(std::move(functions));
    } catch (...) {
        parser::finalize();
        throw;
    }
    if (parser::finalize() != 0)
        throw std::runtime_error("AST 解析器结束失败");
    // 完整构建且解析器正常结束后才替换缓存条目；旧模块可继续被外部持有。
    return cache.register_module(std::move(module))->module;
}

IRBuilder::IRBuilder(FunctionUnit& function)
    : unit_(function), version_(*function.stage_version(IRStage::NonSSA)) {
    // 参数和返回变量在 FunctionUnit 构造时已有 slot，后续引用必须复用。
    for (std::size_t i = 0; i < unit_.slot_table().size(); ++i) {
        const SlotId id{static_cast<std::uint32_t>(i)};
        slots_.emplace(unit_.slot_table().find(id).name, id);
    }
}

IRBuilder::IRBuilder(CommandUnit& command)
    : unit_(command), version_(*command.stage_version()) {}

BasicBlock& IRBuilder::add_block() {
    return version_.add_block(unit_, "bb" + std::to_string(version_.blocks().size()));
}

SlotId IRBuilder::slot(const std::string& key) {
    auto found = slots_.find(key);
    if (found == slots_.end())
        found = slots_.emplace(key, unit_.slot_table().add_slot(key)).first;
    return found->second;
}

std::vector<ValueId> IRBuilder::lower_indices(
    const ast& node, BasicBlock& block) {
    if (node.nodetype != node_list || node.branch.size() == 0)
        throw std::invalid_argument("索引列表无效");
    std::vector<ValueId> indices;
    indices.reserve(node.branch.size());
    for (std::size_t i = 0; i < node.branch.size(); ++i)
        indices.push_back(lower_value(child(node, i), block));
    return indices;
}

Operand IRBuilder::lower_member(
    const ast& node, BasicBlock& block) {
    if (node.nodetype == node_name) return name(node);
    return lower_value(node, block);
}

ValueId IRBuilder::lower_value(const ast& node, BasicBlock& block) {
    set_insert_point(block);
    switch (node.nodetype) {
        case node_number:
            return create_const(static_cast<const numval&>(node).data);
        case node_char_mat:
        case node_text: {
            const auto& text = static_cast<const textNode&>(node).str;
            return create_const(node.nodetype == node_char_mat
                ? std::make_shared<ba_obj>(text.c_str(), ba_char_mat)
                : std::make_shared<ba_obj>(text));
        }
        case node_name:
            return create_load(slot(name(node)));
        case node_struct_get: {
            if (node.branch.size() != 2)
                throw std::invalid_argument("点号取值节点无效");
            const auto& base_node = child(node, 0);
            if (base_node.nodetype == node_name) {
                const auto base_name = name(base_node);
                const auto found = slots_.find(base_name);
                if (found == slots_.end()) {
                    auto member = lower_member(child(node, 1), block);
                    return create_dot_access(base_name, std::move(member));
                }
                const auto base = create_load(found->second);
                auto member = lower_member(child(node, 1), block);
                return create_dot_reference(base, std::move(member));
            }
            const auto base = lower_value(base_node, block);
            auto member = lower_member(child(node, 1), block);
            return create_dot_reference(base, std::move(member));
        }
        case node_cell_get: {
            if (node.branch.size() != 2)
                throw std::invalid_argument("花括号取值节点无效");
            const auto base = lower_value(child(node, 0), block);
            auto indices = lower_indices(child(node, 1), block);
            return create_brace_reference(base, std::move(indices));
        }
        case node_colon: {
            if (node.branch.size() != 2 && node.branch.size() != 3)
                throw std::invalid_argument("冒号操作数数量错误");
            const auto start = lower_value(child(node, 0), block);
            std::optional<ValueId> step;
            if (node.branch.size() == 3)
                step = lower_value(child(node, 1), block);
            const auto stop = lower_value(child(node, node.branch.size() - 1), block);
            return create_colon(start, step, stop);
        }
        case node_horz_list:
        case node_vert_list: {
            if (node.branch.size() == 0)
                return create_const(std::make_shared<ba_obj>(ba_double_mat));
            std::vector<ValueId> items;
            for (std::size_t i = 0; i < node.branch.size(); ++i)
                items.push_back(lower_value(child(node, i), block));
            return create_concat(std::move(items), node.nodetype == node_horz_list
                ? ConcatInst::Axis::Horizontal : ConcatInst::Axis::Vertical);
        }
        case node_cell:
        case node_brace_fenhao_list: {
            const std::size_t rows = node.nodetype == node_cell
                ? (node.branch.size() ? 1U : 0U) : node.branch.size();
            std::size_t cols = 0;
            std::vector<ValueId> items;
            for (std::size_t r = 0; r < rows; ++r) {
                const auto& row = node.nodetype == node_cell ? child(node, 0) : child(node, r);
                if (r == 0) cols = row.branch.size();
                if (row.branch.size() != cols)
                    throw std::invalid_argument("元胞行宽不同");
                for (std::size_t c = 0; c < cols; ++c)
                    items.push_back(lower_value(child(row, c), block));
            }
            return create_cell(std::move(items), rows, cols);
        }
        default: break;
    }
    if (const auto op = unary_op(node.nodetype)) {
        if (node.branch.size() != 1)
            throw std::invalid_argument("一元运算的操作数数量错误");
        return create_unary(lower_value(child(node, 0), block), *op);
    }
    if (const auto op = binary_op(node.nodetype)) {
        if (node.branch.size() != 2)
            throw std::invalid_argument("二元运算的操作数数量错误");
        const auto lhs = lower_value(child(node, 0), block);
        const auto rhs = lower_value(child(node, 1), block);
        return create_binary(lhs, rhs, *op);
    }
    throw std::invalid_argument("尚未支持的表达式 AST 节点");
}

BasicBlock* IRBuilder::lower_if(const ast& node, BasicBlock& current) {
    if (node.branch.size() != 3) throw std::invalid_argument("if 节点无效");
    auto& yes = add_block();
    auto& no = add_block();
    lower_condition(child(node, 0), current, yes, no);
    auto* yes_end = lower_statement(child(node, 1), &yes);
    auto* no_end = lower_statement(child(node, 2), &no);
    // 两条路径都已经终结时，无需创建后继合流块。
    if (!yes_end && !no_end) return nullptr;
    auto& end = add_block();
    if (yes_end) set_insert_point(*yes_end).create_goto(end);
    if (no_end) set_insert_point(*no_end).create_goto(end);
    return &end;
}

BasicBlock* IRBuilder::lower_switch(const ast& node, BasicBlock& current) {
    if (node.branch.size() != 2) throw std::invalid_argument("switch 节点无效");
    const auto& cases = child(node, 1);
    if (cases.nodetype != node_list) throw std::invalid_argument("switch case 列表无效");
    auto& end = add_block();
    const auto selected = lower_value(child(node, 0), current);
    // 按源码顺序串接 case 检查，匹配分支执行完后统一跳到 end。
    BasicBlock* check = &current;
    bool first = true;
    for (std::size_t i = 0; i < cases.branch.size(); ++i) {
        const auto& case_node = child(cases, i);
        if (case_node.branch.size() != 2)
            throw std::invalid_argument("case 节点无效");
        if (case_node.nodetype == node_case) {
            auto& case_block = add_block();
            auto& body_block = add_block();
            if (first) {
                set_insert_point(current).create_goto(case_block);
                first = false;
            } else set_insert_point(*check).create_goto(case_block);
            const auto candidate = lower_value(child(case_node, 0), case_block);
            const auto match = create_switch_match(selected, candidate);
            auto& next = add_block();
            create_branch(match, body_block, next);
            if (auto* tail = lower_statement(child(case_node, 1), &body_block))
                set_insert_point(*tail).create_goto(end);
            check = &next;
        } else if (case_node.nodetype == node_otherwise) {
            auto& body_block = add_block();
            if (first) set_insert_point(current).create_goto(body_block);
            else set_insert_point(*check).create_goto(body_block);
            if (auto* tail = lower_statement(child(case_node, 1), &body_block))
                set_insert_point(*tail).create_goto(end);
            check = nullptr;
            break;
        } else throw std::invalid_argument("未知 case 节点");
    }
    if (first) set_insert_point(current).create_goto(end);
    else if (check) set_insert_point(*check).create_goto(end);
    return &end;
}

BasicBlock* IRBuilder::lower_for(const ast& node, BasicBlock& current) {
    if (node.branch.size() != 3) throw std::invalid_argument("for 节点无效");
    const auto variable = name(child(node, 0));
    auto& preheader = add_block();
    auto& header = add_block();
    auto& body = add_block();
    auto& latch = add_block();
    auto& end = add_block();
    // preheader 求值迭代对象，header 检查下标，latch 推进下标。
    set_insert_point(current).create_goto(preheader);

    // 内部 slot 只通过此 SlotId 引用，不参与用户名字查找。
    const auto index_slot = unit_.slot_table().add_slot(
        "for.index." + preheader.label());
    const auto iterable = lower_value(child(node, 1), preheader);
    const auto state = create_intrinsic(IntrinsicId::ForeachInit, {iterable});
    const auto count = create_intrinsic(IntrinsicId::ForeachCount, {state});
    const auto one = create_const(std::make_shared<ba_obj>(std::int64_t{1}));
    create_store(index_slot, one);
    create_goto(header);

    const auto index = set_insert_point(header).create_load(index_slot);
    const auto done = create_intrinsic(IntrinsicId::IntGt, {index, count});
    create_branch(done, end, body);

    const auto item = set_insert_point(body).create_intrinsic(IntrinsicId::ForeachGet, {state, index});
    create_store(slot(variable), item);
    // 栈顶始终属于最近一层循环，确保嵌套 break/continue 跳转正确。
    loops_.push(end, latch);
    auto* body_end = lower_statement(child(node, 2), &body);
    loops_.pop();
    if (body_end) set_insert_point(*body_end).create_goto(latch);

    const auto old_index = set_insert_point(latch).create_load(index_slot);
    const auto next = create_intrinsic(IntrinsicId::IntAdd, {old_index, one});
    create_store(index_slot, next);
    create_goto(header);
    return &end;
}

BasicBlock* IRBuilder::lower_while(const ast& node, BasicBlock& current) {
    if (node.branch.size() != 3 || node.branch.at(2))
        throw std::invalid_argument("while 节点无效");
    auto& header = add_block();
    auto& body = add_block();
    auto& end = add_block();
    set_insert_point(current).create_goto(header);

    // continue 回到 header 重新计算条件；break 直接跳到 end。
    lower_condition(child(node, 0), header, body, end);

    loops_.push(end, header);
    auto* body_end = lower_statement(child(node, 1), &body);
    loops_.pop();
    if (body_end) set_insert_point(*body_end).create_goto(header);
    return &end;
}

void IRBuilder::lower_condition(
    const ast& node, BasicBlock& current,
    BasicBlock& true_target, BasicBlock& false_target) {
    if (is_short_circuit(node.nodetype)) {
        lower_short_condition(node, current, true_target, false_target);
        return;
    }
    const auto condition = lower_value(node, current);
    create_branch(condition, true_target, false_target);
}

void IRBuilder::lower_short_condition(
    const ast& node, BasicBlock& current,
    BasicBlock& true_target, BasicBlock& false_target) {
    if (is_short_circuit(node.nodetype)) {
        if (node.branch.size() != 2)
            throw std::invalid_argument("短路运算的操作数数量错误");
        // 只在需要时执行右操作数；布尔分支直接连接目标块。
        auto& rhs = add_block();
        if (node.nodetype == node_logic_and_short)
            lower_short_condition(child(node, 0), current, rhs, false_target);
        else
            lower_short_condition(child(node, 0), current, true_target, rhs);
        lower_short_condition(child(node, 1), rhs, true_target, false_target);
        return;
    }
    const auto value = lower_value(node, current);
    const auto logical = create_intrinsic(IntrinsicId::LogicalScalar, {value});
    create_branch(logical, true_target, false_target);
}

BasicBlock* IRBuilder::lower_short_assignment(
    const ast& node, BasicBlock& current, SlotId target) {
    auto& yes = add_block();
    auto& no = add_block();
    auto& end = add_block();
    lower_short_condition(node, current, yes, no);
    const auto true_value = set_insert_point(yes).create_const(std::make_shared<ba_obj>(true));
    create_store(target, true_value);
    create_goto(end);
    const auto false_value = set_insert_point(no).create_const(std::make_shared<ba_obj>(false));
    create_store(target, false_value);
    create_goto(end);
    return &end;
}

BasicBlock* IRBuilder::lower_statement(
    const ast& node, BasicBlock* current) {
    if (!current || node.nodetype == node_nop ||
        node.nodetype == node_andy_end_of_string) return current;
    if (node.nodetype == node_cmdlist || node.nodetype == node_runlist) {
        for (std::size_t i = 0; i < node.branch.size() && current; ++i) {
            if (auto part = node.branch.at(i))
                current = lower_statement(*part, current);
        }
        return current;
    }
    set_insert_point(*current);
    switch (node.nodetype) {
        case node_multiple_func: {
            if (node.branch.size() != 3)
                throw std::invalid_argument("圆括号调用或取值节点无效");
            const auto targets = node.branch.at(0);
            const auto& arguments = child(node, 2);
            if (arguments.nodetype != node_list ||
                (targets && (targets->nodetype != node_list ||
                             targets->branch.size() != 1)))
                throw std::invalid_argument("当前仅支持单结果圆括号表达式");
            const auto callee = name(child(node, 1));
            const auto found = slots_.find(callee);
            std::optional<ValueId> base;
            if (found != slots_.end()) base = create_load(found->second);
            std::vector<ValueId> values;
            for (std::size_t i = 0; i < arguments.branch.size(); ++i)
                values.push_back(lower_value(child(arguments, i), *current));
            if (!targets) {
                if (base) create_value_apply(*base, std::move(values));
                else create_call(callee, std::move(values));
                return current;
            }
            ValueId result = !base
                ? create_apply(callee, std::move(values))
                : create_paren_reference(*base, std::move(values));
            create_store(slot(name(child(*targets, 0))), result);
            return current;
        }
        case node_asgn: {
            if (node.branch.size() != 2) throw std::invalid_argument("赋值节点无效");
            const auto& lhs = child(node, 0);
            if (lhs.nodetype == node_name) {
                const auto target = slot(name(lhs));
                const auto& rhs = child(node, 1);
                if (is_short_circuit(rhs.nodetype))
                    return lower_short_assignment(rhs, *current, target);
                const auto result = lower_value(rhs, *current);
                create_store(target, result);
            } else if (lhs.nodetype == node_multiple_func) {
                if (lhs.branch.size() != 3 || lhs.branch.at(0))
                    throw std::invalid_argument("圆括号赋值目标无效");
                const auto target = slot(name(child(lhs, 1)));
                const auto base = create_load(target);
                auto indices = lower_indices(child(lhs, 2), *current);
                const auto& rhs = child(node, 1);
                const bool deleting = rhs.nodetype == node_vert_list &&
                                      rhs.branch.size() == 0;
                const auto updated = deleting
                    ? create_paren_delete(base, std::move(indices))
                    : create_paren_assign(base, std::move(indices),
                                                  lower_value(rhs, *current));
                create_store(target, updated);
            } else throw std::invalid_argument("尚未支持的赋值目标");
            return current;
        }
        case node_cell_set: {
            if (node.branch.size() != 3)
                throw std::invalid_argument("花括号赋值节点无效");
            const auto target = slot(name(child(node, 0)));
            const auto base = create_load(target);
            auto indices = lower_indices(child(node, 1), *current);
            const auto value = lower_value(child(node, 2), *current);
            const auto updated = create_brace_assign(base, std::move(indices), value);
            create_store(target, updated);
            return current;
        }
        case node_struct_get:
            (void)lower_value(node, *current);
            return current;
        case node_struct_set: {
            if (node.branch.size() != 3)
                throw std::invalid_argument("点号赋值节点无效");
            const auto target = slot(name(child(node, 0)));
            const auto base = create_load(target);
            auto member = lower_member(child(node, 1), *current);
            const auto value = lower_value(child(node, 2), *current);
            const auto updated = create_dot_assign(base, std::move(member), value);
            create_store(target, updated);
            return current;
        }
        case node_global:
        case node_persistent: {
            std::vector<SlotId> slots;
            for (std::size_t i = 0; i < node.branch.size(); ++i)
                slots.push_back(slot(name(child(node, i))));
            if (node.nodetype == node_global) create_global_decl(std::move(slots));
            else create_persistent_decl(std::move(slots));
            return current;
        }
        case node_flow_if: return lower_if(node, *current);
        case node_flow_switch: return lower_switch(node, *current);
        case node_for: return lower_for(node, *current);
        case node_flow_while: return lower_while(node, *current);
        case node_break:
            create_break(loops_);
            return nullptr;
        case node_continue:
            create_continue(loops_);
            return nullptr;
        case node_return:
            create_return({});
            return nullptr;
        default: throw std::invalid_argument(
            std::string("尚未支持的语句 AST 节点: ") +
            ast::nodeTypeString()[node.nodetype]);
    }
}

void IRBuilder::build(const ast& root) {
    if (version_.entry_block())
        throw std::invalid_argument("当前代码单元已有 Non-SSA 基本块");
    const ast* body = nullptr;
    if (root.nodetype == node_runlist) body = &root;
    else if (root.nodetype == node_mfile_func && root.branch.size() >= 4)
        body = &child(root, 3);
    else throw std::invalid_argument("预期脚本或函数 AST");
    // 语句 lowering 返回仍可继续执行的尾块；显式 return 后无需补终结指令。
    auto& entry = add_block();
    if (auto* tail = lower_statement(*body, &entry))
        set_insert_point(*tail).create_return({});
}

} // namespace baltam
