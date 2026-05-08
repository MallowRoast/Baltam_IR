#include "ir/ir_type.h"

#include <ostream>

namespace baltam {

std::ostream& operator<<(std::ostream& os, TypeSet type_set) {
    if (type_set == TypeSet::bottom()) {
        return os << "bottom";
    }
    if (type_set == TypeSet::any()) {
        return os << "any";
    }

    bool first = true;
    const auto append_leaf = [&](TypeSet leaf_type, const char* name) {
        if (!type_set.is_superset_of(leaf_type)) {
            return;
        }
        if (!first) {
            os << '|';
        }
        os << name;
        first = false;
    };

    append_leaf(TypeSet::logical(), "logical");
    append_leaf(TypeSet::int64(), "int64");
    append_leaf(TypeSet::uint64(), "uint64");
    append_leaf(TypeSet::float64(), "double");
    append_leaf(TypeSet::complex(), "complex128");
    append_leaf(TypeSet::char_array(), "char_array");
    append_leaf(TypeSet::string_scalar(), "string_scalar");
    append_leaf(TypeSet::cell_array(), "cell_array");
    append_leaf(TypeSet::struct_array(), "struct_array");
    append_leaf(TypeSet::function_handle(), "function_handle");
    append_leaf(TypeSet::external_object(), "extern");

    return os;
}

} // namespace baltam
