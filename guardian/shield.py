import typing
import inspect
from typing import Any

from .guard import guard
from .compiler import compile_rule, format_type_name
from . import _guardian_core

PRIMITIVES = {int, str, float, bool, type(None)}


class shield(_guardian_core.ShieldBase):
  """
  Provides a base for creating shielded classes that enforce attribute-type rules.

  This class is used as a base for defining attribute shielding functionality to control how
  attributes are set and ensure they conform to specified type rules. Subclasses can define
  type annotations for their attributes, which will be enforced by the shielding mechanism.
  The shielding rules are based on type hints and can handle complex rules, including resolving
  forward references and primitives.

  :ivar __shield_rules__: A dictionary mapping attribute names to their compiled type rules
      and expected type names. This is populated during subclass initialization based on
      type annotations.
  :type __shield_rules__: dict[str, tuple[Any, str]]
  """

  __shield_rules__: dict[str, tuple[Any, str]] = {}

  def __init_subclass__(cls, **kwargs):
    super().__init_subclass__(**kwargs)

    cls.__shield_rules__ = {}
    for base in reversed(cls.__mro__):
      if hasattr(base, '__shield_rules__'):
        cls.__shield_rules__.update(base.__shield_rules__)

    # FIX: Resolve string forward references for class attributes
    try:
      annotations = typing.get_type_hints(cls)
    except Exception:
      annotations = inspect.get_annotations(cls)

    for attr_name, attr_type in annotations.items():
      raw_rule = compile_rule(attr_type)
      if attr_type in PRIMITIVES and raw_rule[0] == 1:
        raw_rule = (2, raw_rule[1])

      expected_name = format_type_name(attr_type)
      cls.__shield_rules__[attr_name] = (raw_rule, expected_name)

    if '__init__' in cls.__dict__:
      original_init = cls.__init__

      new_annotations = {'self': Any}
      new_annotations.update(getattr(original_init, '__annotations__', {}))
      original_init.__annotations__ = new_annotations

      cls.__init__ = guard(original_init)