#include "ir/ir_constant.h"

#include "MatTool.h"
#include "ba_obj/ba_obj.h"
#include "ba_obj/cell.h"
#include "ba_obj/matrix.h"
#include "ba_obj/structure.h"

#include <charconv>
#include <cctype>
#include <complex>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace baltam {
namespace {

std::string quote(std::string_view s, char delimiter = '"') {
    std::string out(1, delimiter);
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == delimiter || c == '\\') {
            out += '\\';
            out += c;
        } else if (c < 32 || c >= 127) {
            out += "\\x";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else
            out += c;
    }
    return out + delimiter;
}

template <class T>
T integer(std::string_view s, int base = 10) {
    T result{};
    auto parsed = std::from_chars(s.data(), s.data() + s.size(), result, base);
    if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size())
        throw std::invalid_argument("整数格式无效或超出类型范围");
    return result;
}

struct Cursor {
    std::string_view s;
    std::size_t p = 0;

    void space() {
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
            ++p;
    }

    bool take(char c) {
        space();
        if (p < s.size() && s[p] == c) {
            ++p;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!take(c))
            throw std::invalid_argument(std::string("缺少字符 '") + c + "'");
    }

    std::string token() {
        space();
        auto start = p;
        while (p < s.size() && !std::isspace(static_cast<unsigned char>(s[p])) && s[p] != ',' &&
               s[p] != ']' && s[p] != ')')
            ++p;
        if (p == start)
            throw std::invalid_argument("缺少常量数据");
        return std::string(s.substr(start, p - start));
    }

    std::string quoted(char delim = '"') {
        expect(delim);
        std::string out;
        while (p < s.size()) {
            char c = s[p++];
            if (c == delim)
                return out;
            if (c == '\\') {
                if (p == s.size())
                    throw std::invalid_argument("转义序列不完整");
                c = s[p++];
                if (c == 'x') {
                    if (s.size() - p < 2)
                        throw std::invalid_argument("十六进制转义序列不完整");
                    c = static_cast<char>(integer<unsigned>(s.substr(p, 2), 16));
                    p += 2;
                } else if (c == 'n')
                    c = '\n';
                else if (c == 'r')
                    c = '\r';
                else if (c == 't')
                    c = '\t';
                else if (c != '\\' && c != '"' && c != '\'')
                    throw std::invalid_argument("转义序列无效");
            }
            out += c;
        }
        throw std::invalid_argument("字符串缺少结束引号");
    }


};

template <class T>
std::string encode_element(T x) {
    if constexpr (std::is_floating_point_v<T>) {
        using U = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        U bits;
        std::memcpy(&bits, &x, sizeof x);
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "0x" << std::hex << std::setfill('0') << std::setw(sizeof(T) * 2) << bits;
        return out.str();
    } else if constexpr (std::is_same_v<T, bool>)
        return x ? "true" : "false";
    else if constexpr (std::is_same_v<T, char>)
        return quote(std::string(1, x), '\'');
    else if constexpr (std::is_same_v<T, std::string>)
        return quote(x);
    else if constexpr (std::is_integral_v<T>)
        return std::to_string(x);
    else
        return "(" + encode_element(x.real()) + ", " + encode_element(x.imag()) + ")";
}

template <class T>
T decode_element(Cursor& in) {
    if constexpr (std::is_floating_point_v<T>) {
        auto text = in.token();
        if (text.size() != 2 + sizeof(T) * 2 || text.substr(0, 2) != "0x")
            throw std::invalid_argument("浮点位模式的格式或位宽不正确");
        using U = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        U bits = integer<U>(std::string_view(text).substr(2), 16);
        T result;
        std::memcpy(&result, &bits, sizeof result);
        return result;
    } else if constexpr (std::is_same_v<T, std::string>)
        return in.quoted();
    else if constexpr (std::is_same_v<T, char>) {
        auto s = in.quoted('\'');
        if (s.size() != 1)
            throw std::invalid_argument("字符常量必须恰好包含一个字节");
        return s[0];
    } else if constexpr (std::is_same_v<T, bool>) {
        auto s = in.token();
        if (s != "true" && s != "false")
            throw std::invalid_argument("逻辑常量必须为 true 或 false");
        return s == "true";
    } else if constexpr (std::is_integral_v<T>)
        return integer<T>(in.token());
    else {
        in.expect('(');
        auto real = decode_element<typename T::value_type>(in);
        in.expect(',');
        auto imag = decode_element<typename T::value_type>(in);
        in.expect(')');
        return T(real, imag);
    }
}

// 文件类型名称与运行时矩阵类型的对应关系。
#define SIMPLE_TYPES(X) \
    X(ba_double_mat, double, "double") \
    X(ba_single_mat, float, "single") \
    X(ba_int8_mat, std::int8_t, "int8") \
    X(ba_int16_mat, std::int16_t, "int16") \
    X(ba_int_mat, std::int32_t, "int32") \
    X(ba_int64_mat, std::int64_t, "int64") \
    X(ba_uint8_mat, std::uint8_t, "uint8") \
    X(ba_uint16_mat, std::uint16_t, "uint16") \
    X(ba_uint_mat, std::uint32_t, "uint32") \
    X(ba_uint64_mat, std::uint64_t, "uint64") \
    X(ba_bool_mat, bool, "logical") \
    X(ba_char_mat, char, "char") \
    X(ba_string, std::string, "string") \
    X(ba_complex_double_mat, std::complex<double>, "complex<double>") \
    X(ba_complex_single_mat, std::complex<float>, "complex<single>")

std::string descriptor(const ba_obj& obj) {
    std::string out;
    switch (obj.type()) {
#define NAME(ID, T, N)                                                                             \
    case ID:                                                                                       \
        out = N;                                                                                   \
        break;
        SIMPLE_TYPES(NAME)
#undef NAME
    case ba_cell:
        out = "cell";
        break;
    case ba_struct:
        out = "struct";
        break;
    default:
        throw std::invalid_argument("不支持的常量对象类型：" + std::to_string(obj.type()));
    }
    out += ' ';
    const auto& dims = obj.cget<type_base>()->dim_vec();
    if (dims.size() < 2)
        throw std::invalid_argument("常量至少需要两个维度");
    for (std::size_t i = 0; i < dims.size(); ++i) {
        if (i)
            out += 'x';
        out += std::to_string(dims[i]);
    }
    return out;
}

template <class T>
std::string encode_array(const ba_obj& obj, std::string* display) {
    auto& m = *obj.cget<matrix<T>>();
    if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, char> && !std::is_same_v<T, bool>) {
        if (display && m.size() == 1) {
            std::ostringstream text;
            text.imbue(std::locale::classic());
            text << std::setprecision(17) << +m(0);
            *display = text.str();
        }
    }

    std::string out = "[";
    for (baIndex i = 0; i < m.size(); ++i) {
        if (i)
            out += ", ";
        out += encode_element<T>(m(i));
    }
    return out + "]";
}

std::string payload(const ba_obj& obj, unsigned depth = 0, std::string* display = nullptr) {
    if (depth > 64)
        throw std::invalid_argument("容器嵌套超过 64 层或存在循环引用");
    switch (obj.type()) {
#define LIST(ID, T, N)                                                                             \
    case ID:                                                                                       \
        return encode_array<T>(obj, display);
        SIMPLE_TYPES(LIST)
#undef LIST
    case ba_cell: {
        std::string out = "[";
        for (const auto& item : obj.cget<cell_array>()->data()) {
            if (!item)
                throw std::invalid_argument("元胞常量包含空指针元素");
            auto key = descriptor(*item) + payload(*item, depth + 1);
            out += std::to_string(key.size()) + ":" + key;
        }
        return out + "]";
    }
    case ba_struct: {
        std::string out = "{";
        const auto& object = *obj.cget<structure>();
        for (const auto& field : object.fieldnames()) {
            out += quote(field) + ":";
            auto* values = object.get_field_table(field);
            if (!values)
                throw std::invalid_argument("结构体常量缺少字段数据表");
            for (const auto& item : *values) {
                if (!item)
                    throw std::invalid_argument("结构体常量包含空指针字段");
                auto key = descriptor(*item) + payload(*item, depth + 1);
                out += std::to_string(key.size()) + ":" + key;
            }
        }
        return out + "}";
    }
    default:
        throw std::invalid_argument("不支持该常量对象");
    }
}

struct Shape {
    std::string type;
    std::vector<baSize> dims;
    std::size_t count = 1;
};

Shape parse_shape(const std::string& desc) {
    Shape result;
    auto split = desc.find(' ');
    if (split == std::string::npos)
        throw std::invalid_argument("常量缺少形状");
    result.type = desc.substr(0, split);
    auto dims = desc.substr(split + 1);
    std::size_t start = 0;
    do {
        auto end = dims.find('x', start);
        if (end == std::string::npos)
            end = dims.size();
        auto d = integer<baSize>(std::string_view(dims).substr(start, end - start));
        if (d < 0 || (d && result.count > std::numeric_limits<std::size_t>::max() /
                                              static_cast<std::size_t>(d)))
            throw std::invalid_argument("常量维度无效或元素数量溢出");
        result.count *= d;
        result.dims.push_back(d);
        start = end + 1;
    } while (start <= dims.size());
    if (result.count > static_cast<std::size_t>(std::numeric_limits<baIndex>::max()))
        throw std::invalid_argument("常量元素数量超出运行时索引范围");
    if (result.dims.size() < 2)
        throw std::invalid_argument("常量至少需要两个维度");
    return result;
}

template <class T>
const_ba_obj_ptr decode_array(Cursor& in, const Shape& shape) {
    // 每个元素至少占一个字符，分配内存前先排除明显过大的元素数量。
    if (shape.count > in.s.size())
        throw std::invalid_argument("常量元素数量超过内联数据长度");
    auto obj = std::make_unique<matrix<T>>(shape.dims);
    in.expect('[');
    for (std::size_t i = 0; i < shape.count; ++i) {
        if (i)
            in.expect(',');
        (*obj)(i) = decode_element<T>(in);
    }
    in.expect(']');
    return std::make_shared<ba_obj>(obj.release());
}

struct Line {
    std::string label, desc;
    ValueId result;
};
Line parse_line(std::string_view text) {
    static const std::regex expression(
        R"(^\s*%([0-9]+)\s*=\s*const\s+(!c[0-9]+)\s*(?:\([^()]*\))?\s*:\s*(\S+\s+[0-9]+(?:x[0-9]+)+)\s*$)");
    std::smatch m;
    std::string s(text);
    if (!std::regex_match(s, m, expression))
        throw std::invalid_argument("常量指令格式无效");
    auto shape = parse_shape(m[3]);
    std::string desc = shape.type + " ";
    for (std::size_t i = 0; i < shape.dims.size(); ++i) {
        if (i)
            desc += 'x';
        desc += std::to_string(shape.dims[i]);
    }
    return {m[2], desc, ValueId{integer<std::uint32_t>(m[1].str())}};
}

struct MatData {
    std::unordered_map<std::string, bxArray*> objects;
    ~MatData() { MatFreeData(objects); }
};

const_ba_obj_ptr load_mat(const std::filesystem::path& file, const std::string& desc) {
    MatData data;
    try {
        data.objects = MatReadData(file.string().c_str());
    } catch (const std::exception&) {
        throw std::runtime_error("无法读取 MAT 常量文件：" + file.string());
    }
    if (data.objects.size() != 1 || !data.objects.count("value") || !data.objects.at("value"))
        throw std::invalid_argument("MAT 常量文件必须且只能包含变量 value：" + file.string());
    auto* obj = reinterpret_cast<ba_obj*>(data.objects.at("value"));
    if (descriptor(*obj) != desc)
        throw std::invalid_argument("MAT 常量的类型或形状不匹配：" + file.string());
    // 释放 MAT 读取结果前复制对象包装，底层数据由 ba_obj 的写时复制机制共享。
    return std::make_shared<ba_obj>(*obj);
}

} // 匿名命名空间

ConstantEncoder::ConstantEncoder(std::filesystem::path directory, std::filesystem::path constants)
    : directory_(std::move(directory)), constant_directory_(std::move(constants)) {}

std::string ConstantEncoder::encode(const ConstInst& inst) {
    if (!inst.value)
        throw std::invalid_argument("常量对象不能为空");
    auto desc = descriptor(*inst.value);
    const auto count = inst.value->cget<type_base>()->size();
    std::string display;
    auto data = payload(*inst.value, 0, &display);
    auto key = desc + ":" + data;
    auto found = labels_.find(key);
    std::size_t id;
    if (found != labels_.end())
        id = found->second;
    else {
        id = definitions_.size();
        std::string definition = "    !c" + std::to_string(id) + " = ";
        if (inst.value->type() != ba_cell && inst.value->type() != ba_struct && count < 10)
            definition += "inline " + data;
        else {
            if (directory_.empty())
                throw std::invalid_argument("包含 MAT 常量时必须提供资源目录");
            std::error_code error;
            std::filesystem::create_directories(directory_ / constant_directory_, error);
            if (error)
                throw std::runtime_error("无法创建 MAT 常量目录：" +
                                         (directory_ / constant_directory_).string());
            auto relative =
                constant_directory_ / ("c" + std::to_string(id) + "_" +
                                       std::to_string(std::hash<std::string>{}(key)) + ".mat");
            auto file = directory_ / relative;
            const bool exists = std::filesystem::exists(file, error);
            if (error)
                throw std::runtime_error("无法访问 MAT 常量文件：" + file.string());
            try {
                if (!exists) {
                    // 向 MAT 接口传入包装副本，保留原始只读常量。
                    ba_obj copy(*inst.value);
                    std::unordered_map<std::string, bxArray*> objects{
                        {"value", reinterpret_cast<bxArray*>(&copy)}};
                    if (MatWrite(file.string().c_str(), objects, true) != 0)
                        throw std::invalid_argument("无法写入 MAT 常量文件：" + file.string());
                }
                if (payload(*load_mat(file, desc)) != data)
                    throw std::invalid_argument("MAT 常量文件中的数据与原对象不一致：" +
                                                file.string());
            } catch (...) {
                if (!exists) {
                    std::error_code ignored;
                    std::filesystem::remove(file, ignored);
                }
                throw;
            }
            definition += "mat " + quote(relative.generic_string());
        }
        labels_.emplace(std::move(key), id);
        definitions_.push_back(std::move(definition));
    }

    std::string out = "%" + std::to_string(inst.result.index) + " = const !c" + std::to_string(id);
    if (!display.empty())
        out += " (" + display + ")";
    return out + ": " + desc;
}

std::string ConstantEncoder::metadata() const {
    std::string out = "constants {\n";
    for (const auto& line : definitions_)
        out += line + "\n";
    return out + "}\n";
}

ConstantDecoder::ConstantDecoder(std::string_view metadata, const std::vector<std::string>& lines,
                                 std::filesystem::path directory) {
    for (const auto& text : lines) {
        auto line = parse_line(text);
        auto [it, added] = entries_.emplace(line.label, Entry{line.desc, {}});
        if (!added && it->second.descriptor != line.desc)
            throw std::invalid_argument("同一常量标签的类型或形状冲突：" + line.label);
    }
    Cursor in{metadata};
    in.space();
    if (in.s.substr(in.p, 9) != "constants")
        throw std::invalid_argument("缺少 constants 常量表");
    in.p += 9;
    in.expect('{');
    while (!in.take('}')) {
        in.space();
        auto start = in.p;
        if (in.s.substr(in.p, 2) != "!c")
            throw std::invalid_argument("缺少常量标签");
        in.p += 2;
        auto digits = in.p;
        while (in.p < in.s.size() && std::isdigit(static_cast<unsigned char>(in.s[in.p])))
            ++in.p;
        if (in.p == digits)
            throw std::invalid_argument("常量标签格式无效");
        std::string label(in.s.substr(start, in.p - start));
        in.expect('=');
        auto it = entries_.find(label);
        if (it == entries_.end())
            throw std::invalid_argument("常量定义未被引用：" + label);
        if (it->second.value)
            throw std::invalid_argument("常量标签重复定义：" + label);
        in.space();
        if (in.s.substr(in.p, 6) == "inline") {
            in.p += 6;
            in.space();
            const auto shape = parse_shape(it->second.descriptor);
#define READ(ID, T, N)                                                                             \
    if (shape.type == N)                                                                           \
        it->second.value = decode_array<T>(in, shape);
            SIMPLE_TYPES(READ)
#undef READ
            if (!it->second.value)
                throw std::invalid_argument("该类型不能内联保存：" + shape.type);
        } else if (in.s.substr(in.p, 3) == "mat") {
            in.p += 3;
            auto path = std::filesystem::u8path(in.quoted());
            if (path.is_relative() && directory.empty())
                throw std::invalid_argument("包含 MAT 常量时必须提供资源目录");
            it->second.value =
                load_mat(path.is_absolute() ? path : directory / path, it->second.descriptor);
        } else
            throw std::invalid_argument("常量定义必须使用 inline 或 mat");
    }
    in.space();
    if (in.p != in.s.size())
        throw std::invalid_argument("常量表末尾存在多余内容");
    for (const auto& item : entries_)
        if (!item.second.value)
            throw std::invalid_argument("缺少常量定义：" + item.first);
}

std::unique_ptr<ConstInst> ConstantDecoder::decode(std::string_view text) const {
    auto line = parse_line(text);
    auto it = entries_.find(line.label);
    if (it == entries_.end() || it->second.descriptor != line.desc)
        throw std::invalid_argument("常量标签不存在，或类型及形状不匹配：" + line.label);
    return std::make_unique<ConstInst>(it->second.value, line.result);
}

#undef SIMPLE_TYPES

} // baltam 命名空间
