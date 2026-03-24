//
// Created by zj on 25-7-3.
//

#include <cstdlib>

#include "bt_ast_interface.h"

using namespace baltam;

struct Instruction {};

struct node_num : Instruction {
    std::string data;

    template <typename T>
    T as_num() {
        return std::stod(data);
    }
};

struct node_str : Instruction {
    std::string data;
};

struct node_char : Instruction {
    std::string data;
};

struct node_call : Instruction {
    std::string fun;
    std::vector<std::shared_ptr<Instruction>> args;

    node_call() = default;
    explicit node_call(const std::string_view fun) {
        this->fun = fun;
    }
    explicit node_call(const std::string_view fun,
        std::vector<std::shared_ptr<Instruction>> args) {
        this->fun = fun;
        this->args = args;
    }
};

struct node_name : Instruction {
    std::string name;
};

struct node_goto {
    int label;
};

struct node_goto_if : Instruction {
    Instruction* val;
    int label_true;
    int label_false;
};

struct BasicBlock {
    int label;
    std::vector<Instruction*> data;
    std::vector<BasicBlock*> preds;
    std::vector<BasicBlock*> succs;
};

struct CFG {
    BasicBlock* entry = nullptr;
};

std::shared_ptr<Instruction> parse_node(ast_ptr ast) {
    if (ast == nullptr) { return nullptr; }

    std::shared_ptr<Instruction> node = nullptr;
    switch (ast->nodetype) {
    case node_empty:
        break;
    case node_exit:
        node = std::make_shared<node_call>("exit");
        break;
    case node_nop:
        break;
    case node_andy_end_of_string:
        break;
    case node_greater_than:
        {
            std::shared_ptr<Instruction> lhs = parse_node(ast->branch[0]);
            std::shared_ptr<Instruction> rhs = parse_node(ast->branch[1]);
            std::vector<std::shared_ptr<Instruction>> args{lhs, rhs};
            node = std::make_shared<node_call>("gt", args);
            break;
        }
    case node_less_than:
        {
            std::shared_ptr<Instruction> lhs = parse_node(ast->branch[0]);
            std::shared_ptr<Instruction> rhs = parse_node(ast->branch[1]);
            std::vector<std::shared_ptr<Instruction>> args{lhs, rhs};
            node = std::make_shared<node_call>("lt", args);
            break;
        }

    default:
        break;
    }

    return node;
}

CFG parse_cfg(ast_ptr ast) {
    CFG cfg;

    if (ast == nullptr) {
        return cfg;
    }

    int label = 0;
    switch (ast->nodetype) {
        // case
    }

    return cfg;
}

int main() {
    // 初始化
    bt_ast_interface::initialize();

    // 添加搜索路径
    auto flag = bt_ast_interface::append_path("~/Desktop", true);

    // std::string filename = "~/Desktop/test.m";
    //
    // // auto pth = std::filesystem::canonical(std::filesystem::u8path(filename));
    //
    // std::ifstream ifs(pth);
    // std::string msg;
    // if (!ifs.is_open()) {
    //     msg = str_format("文件 '%s' 打开失败。", filename.c_str());
    //     // return pdptr_vec{};
    // }

    // 解析文件

    // {
        std::string msg;
        auto ret = bt_ast_interface::parse_stdin("sin", msg);
        auto ast = ret->ast;
    // }

    auto symptr = std::static_pointer_cast<symref>(ast->branch[0]);

    // 对 ret 进行处理
    // ...


    // 结束，进行整个库的析构
    // 后续无法进行 API 调用
    bt_ast_interface::finalize();

    // The Baltam runtime leaves a joinable background thread behind.
    // Exit immediately after finalize to avoid hitting std::terminate()
    // during global thread-vector destruction.
    std::_Exit(0);
}



