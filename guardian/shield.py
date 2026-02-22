import inspect
from typing import Any

from .guard import guard
from .compiler import compile_rule, format_type_name
from . import _guardian_core


class shield(_guardian_core.ShieldBase):
  """
  Base class that enforces strict typing on class attributes and
  prevents external access to protected/private state.

  Inherits natively from C-Level ShieldBase for 0-overhead property validation.
  """

  __shield_rules__: dict[str, tuple[Any, str]] = {}

  def __init_subclass__(cls, **kwargs):
    super().__init_subclass__(**kwargs)

    # 1. Inherit rules from parent classes (walking MRO backwards)
    cls.__shield_rules__ = {}
    for base in reversed(cls.__mro__):
      if hasattr(base, '__shield_rules__'):
        cls.__shield_rules__.update(base.__shield_rules__)

    # 2. Compile class annotations once at import time
    annotations = inspect.get_annotations(cls)
    for attr_name, attr_type in annotations.items():
      compiled_rule = compile_rule(attr_type)
      expected_name = format_type_name(attr_type)
      cls.__shield_rules__[attr_name] = (compiled_rule, expected_name)

    # 3. Enforce __init__ signature dynamically
    if '__init__' in cls.__dict__:
      original_init = cls.__init__

      # Inject 'self' to align positional args perfectly in the C vectorcall
      new_annotations = {'self': Any}
      new_annotations.update(getattr(original_init, '__annotations__', {}))
      original_init.__annotations__ = new_annotations

      cls.__init__ = guard(original_init)