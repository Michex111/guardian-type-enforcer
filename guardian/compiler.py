import types
import typing
from typing import Any, Literal, get_args, get_origin, Annotated, Union

# Must match C header macros
OP_ANY = 0
OP_INSTANCE = 1
OP_EXACT = 2
OP_UNION = 3
OP_LIST = 4
OP_DICT = 5
OP_TUPLE_VAR = 6
OP_TUPLE_FIXED = 7
OP_SET = 8
OP_LITERAL = 9


def format_type_name(tp: Any) -> str:
    origin = get_origin(tp)
    args = get_args(tp)
    if origin is Union or isinstance(tp, types.UnionType):
        return " | ".join(format_type_name(a) for a in args)
    if origin is Literal:
        return f"Literal[{', '.join(repr(a) for a in args)}]"
    if origin is Annotated:
        return format_type_name(args[0])
    if tp is Ellipsis:
        return "..."
    if origin:
        base = getattr(origin, "__name__", str(origin).replace("typing.", ""))
        if args:
            return f"{base}[{', '.join(format_type_name(a) for a in args)}]"
        return base
    return getattr(tp, "__name__", str(tp).replace("typing.", ""))


def compile_rule(expected_type: Any) -> tuple:
    if expected_type is Any:
        return (OP_ANY, None)

    origin = get_origin(expected_type)
    args = get_args(expected_type)

    if origin is Annotated:
        return compile_rule(args[0])

    if origin is Union or isinstance(expected_type, types.UnionType):
        return (OP_UNION, tuple(compile_rule(a) for a in args))

    if origin is Literal:
        return (OP_LITERAL, tuple(args))

    if origin is list:
        if not args: return (OP_INSTANCE, list)
        return (OP_LIST, compile_rule(args[0]))

    if origin is set:
        if not args: return (OP_INSTANCE, set)
        return (OP_SET, compile_rule(args[0]))

    if origin is dict:
        if not args: return (OP_INSTANCE, dict)
        return (OP_DICT, (compile_rule(args[0]), compile_rule(args[1])))

    if origin is tuple:
        if not args: return (OP_INSTANCE, tuple)
        if len(args) == 2 and args[1] is Ellipsis:
            return (OP_TUPLE_VAR, compile_rule(args[0]))
        return (OP_TUPLE_FIXED, tuple(compile_rule(a) for a in args))

    target_type = origin or expected_type
    if not isinstance(target_type, type):
        target_type = type(None) if expected_type is type(None) else Any

    if target_type is Any:
        return (OP_ANY, None)

    return (OP_INSTANCE, target_type)