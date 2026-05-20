import typing
import inspect
import ast
import textwrap
from typing import Callable, Any

from ._compiler import compile_rule, format_type_name
from . import _guardian_core

PRIMITIVES = {int, str, float, bool, type(None)}


def _extract_local_annotations(func: Callable) -> dict:
    """Parses the function AST to extract local variable annotations."""
    local_hints = {}
    try:
        source = textwrap.dedent(inspect.getsource(func))
        tree = ast.parse(source)
    except Exception:
        return local_hints

    for node in ast.walk(tree):
        # Look for explicit variable annotations (e.g., var: type)
        if isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            var_name = node.target.id
            try:
                ann_str = ast.unparse(node.annotation)
                # Evaluate in the function's global context to resolve standard types
                resolved_type = eval(ann_str, func.__globals__)
                local_hints[var_name] = resolved_type
            except Exception:
                pass
    return local_hints

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


def guard(func=None, *, check_return: bool = True) -> Callable:
  """
  Creates a guarded version of the provided function, enforcing rules defined
  by the compiled signature. This ensures that the input parameters and return
  value adhere to the specified constraints during runtime.

  :param func: The function to be wrapped and guarded.
  :param check_return: If False, skips validating the function's return type for maximum performance.
  :return: A new function with the guarding behavior applied.
  """
  # Handle the case where the decorator is called with arguments: @guard(check_return=False)
  if func is None:
    return lambda f: guard(f, check_return=check_return)

  pos_rules, kw_rules, ret_rule, ret_name, has_return_annotation = _compile_signature(func)

  # Only enforce if the user wants it AND the function actually has a return annotation
  enforce_return = check_return and has_return_annotation

  return _guardian_core.make_guard(func, pos_rules, kw_rules, ret_rule, ret_name, enforce_return)


def deepguard(func=None, *, check_return: bool = True) -> Callable:
  """
    Apply a strict, tracing-based guarding mechanism for deep runtime auditing.

    Unlike standard `@guard`, `deepguard` hooks directly into the Python evaluator
    to monitor internal frame execution. It ensures that not only the boundaries
    (inputs and outputs) are strictly typed, but it also prevents invalid internal
    mutations of tracked variables during the function's lifecycle.

    Because of its deep tracing nature, `deepguard` incurs performance overhead
    and is best utilized as a strict development, debugging, or auditing tool
    rather than in high-performance production loops.

    :param func: The target function to be deeply audited. If not provided,
        the decorator can be applied with additional configuration options.
    :type func: Callable, optional
    :param check_return: If True, validates the return value against its type
        annotation upon function exit. Defaults to True.
    :return: A strictly guarded function that performs deep runtime type enforcement.
    :rtype: Callable
  """
  if func is None:
    return lambda f: deepguard(f, check_return=check_return)

  pos_rules, kw_rules, ret_rule, ret_name, has_return_annotation = _compile_signature(func)
  enforce_return = check_return and has_return_annotation

  # Dynamically parse and append local variable annotations to the C-tracker
  local_annotations = _extract_local_annotations(func)
  for var_name, annotation in local_annotations.items():
      if var_name not in kw_rules:
          raw_rule = compile_rule(annotation)
          if annotation in PRIMITIVES and raw_rule[0] == 1:
              raw_rule = (2, raw_rule[1])
          kw_rules[var_name] = (var_name, format_type_name(annotation), raw_rule)

  return _guardian_core.make_strictguard(func, pos_rules, kw_rules, ret_rule, ret_name, enforce_return)