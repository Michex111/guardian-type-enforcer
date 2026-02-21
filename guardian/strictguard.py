import inspect
from functools import wraps
from typing import Callable, Optional, get_type_hints
from ._guardian_core import make_strictguard
from .compiler import compile_rule, format_type_name


def strictguard(func: Optional[Callable] = None, *, check_return: bool = True) -> Callable:
  if func is None:
    return lambda f: strictguard(f, check_return=check_return)

  if inspect.iscoroutinefunction(func) or inspect.isgeneratorfunction(func):
    raise NotImplementedError("strictguard does not support async or generator functions.")

  sig = inspect.signature(func)
  try:
    hints = get_type_hints(func, include_extras=True)
  except Exception:
    hints = getattr(func, "__annotations__", {})

  rules_list = []
  kw_names = []

  for name, param in sig.parameters.items():
    if name in hints:
      rule = compile_rule(hints[name])
      t_name = format_type_name(hints[name])
      rules_list.append((name, t_name, rule))
      kw_names.append(name)

  rules_tuple = tuple(rules_list)
  kw_names_tuple = tuple(kw_names)

  ret_rule = None
  ret_name = None
  if check_return and "return" in hints:
    ret_rule = compile_rule(hints["return"])
    ret_name = format_type_name(hints["return"])

  c_wrapper = make_strictguard(func, rules_tuple, kw_names_tuple, ret_rule, ret_name, check_return)

  @wraps(func)
  def wrapper(*args, **kwargs):
    return c_wrapper(*args, **kwargs)

  wrapper.__call__ = c_wrapper
  wrapper._c_guard = c_wrapper
  return wrapper