#ifndef BALTAM_IR_INTERPRETER_H
#define BALTAM_IR_INTERPRETER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core.h"
#include "ir/ir.h"

namespace baltam {

/**
 * @brief IR 解释器统一使用的运行时值类型。
 */
using Value = std::shared_ptr<ba_obj>;

/**
 * @brief 运行时符号表中的单个绑定项。
 */
struct Binding {
    Value value;
    bool initialized = false;
};

/**
 * @brief 执行一个 IR Function 时使用的运行时帧。
 *
 * 当前版本采用按名字管理变量的符号表，便于和现有基于名字的 IR
 * 对接。后续若引入 slot 化，可以在不改变外层解释器接口的前提下
 * 替换内部存储。
 */
class Frame {
public:
    /**
     * @brief 运行时符号表类型。
     */
    using SymbolTable = std::unordered_map<std::string, Binding>;

    /**
     * @brief 构造一个执行帧。
     *
     * @param function 当前执行的函数。
     * @param caller 调用者帧；顶层脚本执行时可以为 nullptr。
     * @param nargin 本次调用的输入参数个数。
     * @param nargout 本次调用期望的输出参数个数。
     */
    Frame(Function* function = nullptr, Frame* caller = nullptr, int nargin = 0, int nargout = 0);

    /**
     * @brief 返回当前执行的函数。
     */
    Function* function() const;

    /**
     * @brief 返回调用者帧。
     */
    Frame* caller() const;

    /**
     * @brief 返回本次调用的输入参数个数。
     */
    int nargin() const;

    /**
     * @brief 返回本次调用期望的输出参数个数。
     */
    int nargout() const;

    /**
     * @brief 返回当前函数是否已经执行到 return。
     */
    bool returned() const;

    /**
     * @brief 返回只读符号表。
     */
    const SymbolTable& symbols() const;

    /**
     * @brief 返回当前帧已经收集好的输出值列表。
     */
    const std::vector<Value>& outputs() const;

    /**
     * @brief 预声明一个名字，但不写入值。
     */
    void declare(const std::string& name);

    /**
     * @brief 向指定名字写入一个值，并标记为已初始化。
     */
    void store(const std::string& name, Value value);

    /**
     * @brief 读取一个名字对应的值。
     *
     * 若名字不存在或尚未初始化，会抛出异常。
     */
    Value load(const std::string& name) const;

    /**
     * @brief 判断符号表中是否存在指定名字。
     */
    bool contains(const std::string& name) const;

    /**
     * @brief 判断指定名字是否已经初始化。
     */
    bool is_initialized(const std::string& name) const;

    /**
     * @brief 更新 return 标记。
     */
    void set_returned(bool returned);

    /**
     * @brief 设置当前帧的输出值列表。
     */
    void set_outputs(std::vector<Value> outputs);

private:
    Function* function_ = nullptr;
    Frame* caller_ = nullptr;
    int nargin_ = 0;
    int nargout_ = 0;
    bool returned_ = false;
    SymbolTable symbols_;
    std::vector<Value> outputs_;
};

/**
 * @brief 执行一个 IR 函数。
 *
 * @param function 待执行的函数。
 * @param args 本次调用的输入参数列表。
 * @param caller 调用者帧；顶层调用时可以为 nullptr。
 * @return 执行结束后的 Frame，其中包含符号表和输出值。
 */
Frame execute_function(Function& function, const std::vector<Value>& args = {}, Frame* caller = nullptr);

/**
 * @brief 将一个运行时值转成便于调试输出的短文本。
 */
std::string value_text(const Value& value);

}  // namespace baltam

#endif
