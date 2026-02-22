import typing
import inspect
from typing import Callable, Any

from .compiler import compile_rule, format_type_name
from . import _guardian_core

PRIMITIVES = {int, str, float, bool, type(None)}


def _compile_signature(func: Callable):
  """Pre-compiles signatures into aligned positional arrays and kwarg dictionaries for O(1) C-lookups."""

  # FIX: Resolve string forward references (like 'int') into actual type objects
  try:
    resolved_hints = typing.get_type_hints(func)
  except Exception:
    resolved_hints = getattr(func, '__annotations__', {})

  sig = inspect.signature(func)
  pos_rules = []
  kw_rules = {}

  for name, param in sig.parameters.items():
    # Read the resolved actual type, falling back to empty if un-annotated
    annotation = resolved_hints.get(name, inspect.Parameter.empty)

    if annotation != inspect.Parameter.empty:
      raw_rule = compile_rule(annotation)
      if annotation in PRIMITIVES and raw_rule[0] == 1:
        raw_rule = (2, raw_rule[1])

      rule_def = (name, format_type_name(annotation), raw_rule)
      kw_rules[name] = rule_def

      if param.kind in (inspect.Parameter.POSITIONAL_ONLY, inspect.Parameter.POSITIONAL_OR_KEYWORD):
        pos_rules.append(rule_def)
    else:
      if param.kind in (inspect.Parameter.POSITIONAL_ONLY, inspect.Parameter.POSITIONAL_OR_KEYWORD):
        pos_rules.append(None)

  ret_rule = None
  ret_name = ""
  check_return = False

  # FIX: Handle resolved return annotations
  if 'return' in resolved_hints:
    ret_annotation = resolved_hints['return']
    ret_rule = compile_rule(ret_annotation)
    if ret_annotation in PRIMITIVES and ret_rule[0] == 1:
      ret_rule = (2, ret_rule[1])
    ret_name = format_type_name(ret_annotation)
    check_return = True

  return tuple(pos_rules), kw_rules, ret_rule, ret_name, check_return


def guard(func: Callable) -> Callable:
  pos_rules, kw_rules, ret_rule, ret_name, check_return = _compile_signature(func)
  return _guardian_core.make_guard(func, pos_rules, kw_rules, ret_rule, ret_name, check_return)


def deepguard(func: Callable) -> Callable:
  pos_rules, kw_rules, ret_rule, ret_name, check_return = _compile_signature(func)
  return _guardian_core.make_strictguard(func, pos_rules, kw_rules, ret_rule, ret_name, check_return)