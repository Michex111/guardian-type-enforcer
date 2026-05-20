import dataclasses
import typing
from typing import dataclass_transform, TypeVar, Callable, Any

from ._compiler import compile_rule, format_type_name
from . import _guardian_core
from ._serialization import dump_dict

asdict = dump_dict  # Alias for convenience

T = TypeVar("T")


# 1. Define the custom exception, inheriting from the standard one for compatibility
class FrozenInstanceError(dataclasses.FrozenInstanceError):
    """Raised when attempting to modify a frozen guardian dataclass."""
    pass

# 2. Define the custom assignment hooks to throw your branded error
def _frozen_setattr(self, name: str, value: Any) -> None:
    raise FrozenInstanceError(f"Cannot assign to field {name!r} on frozen guardian dataclass {type(self).__name__!r}")

def _frozen_delattr(self, name: str) -> None:
    raise FrozenInstanceError(f"Cannot delete field {name!r} on frozen guardian dataclass {type(self).__name__!r}")


@dataclass_transform(field_specifiers=(dataclasses.Field, dataclasses.field))
def dataclass(
    cls: type[T] | None = None,
    *,
    init: bool = True,
    repr: bool = True,
    eq: bool = True,
    order: bool = False,
    unsafe_hash: bool = False,
    frozen: bool = False,
    match_args: bool = True,
    kw_only: bool = False,
    slots: bool = False,
    weakref_slot: bool = False,
    strict: bool = False,
) -> type[T] | Callable[[type[T]], type[T]]:
    
    std_kwargs = {
        "init": init,
        "repr": repr,
        "eq": eq,
        "order": order,
        "unsafe_hash": unsafe_hash,
        "frozen": frozen,
        "match_args": match_args,
        "kw_only": kw_only,
        "slots": slots,
        "weakref_slot": weakref_slot,
    }

    def wrap(cls: type[T]) -> type[T]:
        # 1. Standard library generation
        dc_cls = dataclasses.dataclass(**std_kwargs)(cls)
        
        hints = typing.get_type_hints(dc_cls)
        custom_validators = {}
        
        for attr_name in dir(dc_cls):
            attr = getattr(dc_cls, attr_name)
            if hasattr(attr, "__guardian_validator__"):
                custom_validators[attr.__guardian_validator__] = attr

        compiled_rules = {}

        # 2. Inject C-Descriptors
        
        for field in dataclasses.fields(dc_cls): # type: ignore[arg-type]
            expected_type = hints.get(field.name, typing.Any)
            raw_rule = compile_rule(expected_type)
            expected_name = format_type_name(expected_type)
            
            custom_val = custom_validators.get(field.name, None)
            
            compiled_rules[field.name] = (raw_rule, expected_name, custom_val)

            if not frozen:
                c_descriptor = _guardian_core.make_c_descriptor(
                    field.name, 
                    f"_{field.name}", 
                    raw_rule, 
                    expected_name, 
                    custom_val
                )
                setattr(dc_cls, field.name, c_descriptor)

        # 3. Frozen Model Handling
        if frozen:
            original_init = dc_cls.__init__
            def frozen_init(self, *args, **init_kwargs):
                original_init(self, *args, **init_kwargs)
                for name, (rule, exp_name, custom_v) in compiled_rules.items():
                    val = getattr(self, name)
                    c_desc = _guardian_core.make_c_descriptor(name, f"_{name}", rule, exp_name, custom_v)
                    c_desc.__set__(self, val) 
            dc_cls.__init__ = frozen_init
            
            # --- OVERRIDE STANDARD FREEZE HOOKS HERE ---
            dc_cls.__setattr__ = _frozen_setattr
            dc_cls.__delattr__ = _frozen_delattr

        
        return dc_cls

    if cls is None:
        return wrap
    return wrap(cls)

# ----------------------------------- Validator Decorator ----------------------------------- #


def validator(field_name: str) -> Callable:
    """
    Decorator to mark a method as a custom field validator.
    """
    def wrapper(func: Callable):
        func.__guardian_validator__ = field_name
        return func
    return wrapper