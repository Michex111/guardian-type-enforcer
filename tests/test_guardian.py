import pytest
from guardian import guard, strictguard, GuardianTypeError


def test_guard_builtins():
  @guard
  def func(x: int, y: str) -> bool:
    return True

  assert func(1, "test") is True
  with pytest.raises(GuardianTypeError):
    func("1", "test")


def test_guard_collections():
  @guard
  def process(data: list[int], mapping: dict[str, float]) -> set[str]:
    return set(mapping.keys())

  assert process([1, 2], {"a": 1.5}) == {"a"}

  with pytest.raises(GuardianTypeError, match="list\\[int\\]"):
    process([1, "2"], {"a": 1.5})


def test_strictguard_locals():
  @strictguard
  def strict_math(x: int) -> int:
    y = 10
    y = 20
    y = "bad"  # Should fail
    return x + y

  with pytest.raises(GuardianTypeError, match="Variable 'y' expected int"):
    strict_math(5)


def test_strictguard_recursion():
  @strictguard
  def fact(n: int) -> int:
    if n == 0: return 1
    return n * fact(n - 1)

  assert fact(5) == 120