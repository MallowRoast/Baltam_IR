#include "ir/ir_type.h"

#include <ostream>

namespace baltam {

const char* type_atom_name(TypeAtom atom) noexcept {
    switch (atom) {
        case TypeAtom::Logical:
            return "logical";
        case TypeAtom::Int64:
            return "int64";
        case TypeAtom::UInt64:
            return "uint64";
        case TypeAtom::Float64:
            return "float64";
        case TypeAtom::Complex:
            return "complex128";
        case TypeAtom::Char:
            return "char_array";
        case TypeAtom::String:
            return "string_scalar";
        case TypeAtom::Cell:
            return "cell_array";
        case TypeAtom::Struct:
            return "struct_array";
        case TypeAtom::FunctionHandle:
            return "function_handle";
    }
    return "<unknown-type-atom>";
}

std::ostream& operator<<(std::ostream& os, TypeSet type_set) {
    if (type_set == bottom_type_set()) {
        return os << "bottom";
    }
    if (type_set == any_type_set()) {
        return os << "any";
    }

    bool first = true;
    const auto append_atom = [&](TypeAtom atom) {
        if (!type_set.contains(atom)) {
            return;
        }
        if (!first) {
            os << '|';
        }
        os << type_atom_name(atom);
        first = false;
    };

    append_atom(TypeAtom::Logical);
    append_atom(TypeAtom::Int64);
    append_atom(TypeAtom::UInt64);
    append_atom(TypeAtom::Float64);
    append_atom(TypeAtom::Complex);
    append_atom(TypeAtom::Char);
    append_atom(TypeAtom::String);
    append_atom(TypeAtom::Cell);
    append_atom(TypeAtom::Struct);
    append_atom(TypeAtom::FunctionHandle);

    const TypeSet::bits_type unknown_bits = type_set.bits & ~all_known_type_bits();
    if (unknown_bits != 0) {
        if (!first) {
            os << '|';
        }
        os << "<unknown-bits:" << unknown_bits << '>';
    }

    return os;
}

} // namespace baltam
