#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace baltam {

// 与运行时类型枚举保持一致，具体枚举项由 ba_type.h 定义。
enum ba_type : unsigned char;

// 编号的范围和绑定状态由所属代码单元的表检查。
struct ValueId {
    std::uint32_t index;

    constexpr bool operator==(ValueId rhs) const noexcept { return index == rhs.index; }
    constexpr bool operator!=(ValueId rhs) const noexcept { return !(*this == rhs); }
};

struct SlotId {
    std::uint32_t index;

    constexpr bool operator==(SlotId rhs) const noexcept { return index == rhs.index; }
    constexpr bool operator!=(SlotId rhs) const noexcept { return !(*this == rhs); }
};

static_assert(sizeof(ValueId) == sizeof(std::uint32_t));
static_assert(sizeof(SlotId) == sizeof(std::uint32_t));

// 当前用字符串保存名字文本；后续可接入真正的字符串池。
using InternedString = std::string;

// 操作数只表示已求值的结果或尚未消歧的名字；已绑定变量须先通过 LoadSlotInst 取得 ValueId。
using Operand = std::variant<ValueId, InternedString>;

class Instruction;
class IRStageVersion;
class ValueTable;
class BasicBlock;
class Use;
class User;

// 指令结果或函数参数对应一个地址稳定的值对象；编号仅在所属阶段内有效。
class Value final {
public:
    enum class Form : std::uint8_t { Single, MaybePack };

    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    Value(Value&&) = delete;
    Value& operator=(Value&&) = delete;

    [[nodiscard]] ValueId id() const noexcept { return id_; }

    // 参数由函数签名定义，没有对应指令；未绑定的前向引用也没有指令。
    [[nodiscard]] bool has_definition() const noexcept { return is_parameter_ || definition_; }

    [[nodiscard]] bool is_parameter() const noexcept { return is_parameter_; }

    [[nodiscard]] Instruction* definition() const noexcept { return definition_; }

    [[nodiscard]] std::uint32_t result_index() const noexcept { return result_index_; }

    [[nodiscard]] Form form() const noexcept { return form_; }

    [[nodiscard]] bool is_constant() const noexcept { return is_constant_; }

    void set_constant(bool constant) noexcept { is_constant_ = constant; }

    // 静态已知事实；空类型或形状表示未知，形状中的空维度输出为 ?。
    // 这些未知事实与运行时 Undefined 无关。
    [[nodiscard]] std::optional<ba_type> type() const noexcept { return type_; }

    [[nodiscard]] const std::optional<std::vector<std::optional<std::size_t>>>&
    shape() const noexcept {
        return shape_;
    }

    void set_type(std::optional<ba_type> type) noexcept { type_ = type; }

    void set_shape(std::optional<std::vector<std::optional<std::size_t>>> shape) {
        shape_ = std::move(shape);
    }

    [[nodiscard]] bool use_empty() const noexcept { return first_use_ == nullptr; }

    [[nodiscard]] const Use* first_use() const noexcept { return first_use_; }

private:
    friend class Use;
    friend class ValueTable;
    friend class IRStageVersion;

    explicit Value(ValueId id) : id_(id) {}

    ValueId id_;
    Instruction* definition_ = nullptr;
    std::uint32_t result_index_ = 0; // 指令结果位置，参数则为签名中的位置。
    bool is_parameter_ = false;
    Form form_ = Form::Single;
    bool is_constant_ = false;
    Use* first_use_ = nullptr;
    std::optional<ba_type> type_;
    std::optional<std::vector<std::optional<std::size_t>>> shape_;
};

// 每个输入位置独立拥有一条引用，空值表示 Undefined。
class Use final {
public:
    ~Use();
    Use(const Use&) = delete;
    Use& operator=(const Use&) = delete;
    Use(Use&&) = delete;
    Use& operator=(Use&&) = delete;

    [[nodiscard]] User& user() const noexcept { return *user_; }

    [[nodiscard]] std::size_t operand_index() const noexcept { return operand_index_; }

    [[nodiscard]] std::optional<ValueId> value() const noexcept;
    [[nodiscard]] const Value* target() const noexcept { return target_; }

    [[nodiscard]] const Use* next_use() const noexcept { return next_; }

    void set(std::optional<ValueId> value);

private:
    friend class User;
    friend class IRStageVersion;

    Use(User& user, std::size_t index) : user_(&user), operand_index_(index) {}

    void unlink() noexcept;

    User* user_;
    std::size_t operand_index_;
    Value* target_ = nullptr;
    Use* previous_ = nullptr;
    Use* next_ = nullptr;
    std::optional<ValueId> pending_value_;
};

// 输入只存放在 Use 中，子类通过访问器暴露各输入的语义。
class User {
public:
    virtual ~User();
    User& operator=(const User&) = delete;
    User(User&&) = delete;
    User& operator=(User&&) = delete;

    [[nodiscard]] std::size_t operand_count() const noexcept { return operands_.size(); }

    [[nodiscard]] const Use& operand(std::size_t index) const { return *operands_.at(index); }

    void set_operand(std::size_t index, std::optional<ValueId> value);
    void drop_all_references() noexcept;

protected:
    User() = default;
    User(const User& other);
    void append_operand(std::optional<ValueId> value);
    void insert_operand(std::size_t index, std::optional<ValueId> value);
    void erase_operand(std::size_t index);
    [[nodiscard]] ValueId input(std::size_t index) const;
    void append_values(const std::vector<ValueId>& values);
    [[nodiscard]] std::vector<ValueId> input_values(std::size_t offset = 0) const;
    using NamedOperand = std::variant<std::size_t, InternedString>;
    [[nodiscard]] NamedOperand append_named_operand(const Operand& value);
    [[nodiscard]] Operand named_operand(const NamedOperand& value) const;

private:
    friend class Use;
    friend class BasicBlock;
    friend class IRStageVersion;

    void bind(IRStageVersion& version);
    IRStageVersion* version_ = nullptr;
    std::vector<std::unique_ptr<Use>> operands_;
};

} // baltam 命名空间
