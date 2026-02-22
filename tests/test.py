import pytest
import threading
import timeit
from typing import List, Dict, Union, Optional, Any
from pydantic import BaseModel, ValidationError
from beartype import beartype
from beartype.roar import BeartypeCallHintParamViolation, BeartypeCallHintReturnViolation


from guardian.guard import guard
from guardian.deepguard import deepguard
from guardian.shield import shield
from guardian._guardian_core import GuardianTypeError, GuardianAccessError


# ==========================================
# TEST MODELS & SETUP
# ==========================================

@guard
def guard_add(a: int, b: int) -> int:
  return a + b


@deepguard
def deepguard_add(a: int, b: int) -> int:
  return a + b


@beartype
def beartype_add(a: int, b: int) -> int:
  return a + b


def standard_add(a: int, b: int) -> int:
  return a + b


@guard
def guard_nested(data: List[Dict[str, Union[int, float]]]) -> int:
  return len(data)


@guard
def guard_optional(val: Optional[str]) -> str:
  return val if val else "default"


class Cat(shield):
  name: str
  age: int
  _internal_id: int

  def __init__(self, name: str, age: int):
    self.name = name
    self.age = age
    self._internal_id = 123

  def _private_method(self) -> str:
    return "hidden"

  def get_internal_id(self) -> int:
    return self._internal_id

  def call_private(self) -> str:
    return self._private_method()


class PydanticCat(BaseModel):
  name: str
  age: int


# ==========================================
# PART 1 — FUNCTIONAL TESTS
# ==========================================

def test_guard_correct_types():
  assert guard_add(1, 2) == 3


def test_guard_incorrect_types():
  with pytest.raises(GuardianTypeError):
    guard_add(1, "2")


def test_guard_nested_types():
  assert guard_nested([{"a": 1.5, "b": 2}]) == 1
  with pytest.raises(GuardianTypeError):
    guard_nested([{"a": 1.5, "b": "2"}])


def test_guard_optional_and_union():
  assert guard_optional("hello") == "hello"
  assert guard_optional(None) == "default"
  with pytest.raises(GuardianTypeError):
    guard_optional(123)


@guard
def guard_return_bad() -> int:
  return "not an int"


def test_guard_return_enforcement():
  with pytest.raises(GuardianTypeError):
    guard_return_bad()


def test_strictguard_behavior():
  assert deepguard_add(5, 5) == 10
  with pytest.raises(GuardianTypeError):
    deepguard_add(5, 5.0)


@deepguard
def deepguard_local_mutation(val: int) -> int:
  x: int = val
  if val < 0:
    x = "string"  # Should trigger strict local type checking
  return x


def test_deepguard_stricter_mismatch():
  assert deepguard_local_mutation(5) == 5
  with pytest.raises(GuardianTypeError):
    deepguard_local_mutation(-1)


def test_shield_correct_initialization():
  cat = Cat("Luna", 3)
  assert cat.name == "Luna"
  assert cat.age == 3


def test_shield_wrong_init_types():
  with pytest.raises(GuardianTypeError):
    Cat("Luna", "three")


def test_shield_wrong_attribute_assignment():
  cat = Cat("Luna", 3)
  with pytest.raises(GuardianTypeError):
    cat.age = 3.5


def test_shield_access_control():
  cat = Cat("Luna", 3)

  # Internal access allowed
  assert cat.get_internal_id() == 123
  assert cat.call_private() == "hidden"

  # External access blocked
  with pytest.raises(GuardianAccessError):
    _ = cat._internal_id

  with pytest.raises(GuardianAccessError):
    cat._internal_id = 999

  with pytest.raises(GuardianAccessError):
    cat._private_method()


# ==========================================
# PART 2 — EDGE CASE TESTING
# ==========================================

class PremiumCat(Cat):
  pedigree: bool

  def __init__(self, name: str, age: int, pedigree: bool):
    super().__init__(name, age)
    self.pedigree = pedigree


def test_shield_inheritance():
  p_cat = PremiumCat("Bella", 4, True)
  assert p_cat.name == "Bella"
  assert p_cat.pedigree is True

  with pytest.raises(GuardianTypeError):
    p_cat.pedigree = "Yes"

  with pytest.raises(GuardianTypeError):
    p_cat.age = "four"


def test_large_nested_data_structures():
  large_data = [{"k": i * 1.0} for i in range(1000)]
  assert guard_nested(large_data) == 1000

  large_data.append({"k": "bad"})
  with pytest.raises(GuardianTypeError):
    guard_nested(large_data)


def test_high_frequency_calls():
  # 10k iterations within a test to ensure no memory leaks or stack overflow
  cat = Cat("Fast", 1)
  for i in range(10000):
    assert guard_add(i, i) == i * 2
    cat.age = i
  assert cat.age == 9999


def test_thread_safety():
  exceptions = []

  def worker():
    try:
      c = Cat("Threaded", 0)
      for i in range(1000):
        c.age = i
        guard_add(i, i)
    except Exception as e:
      exceptions.append(e)

  threads = [threading.Thread(target=worker) for _ in range(10)]
  for t in threads: t.start()
  for t in threads: t.join()

  assert len(exceptions) == 0


# ==========================================
# PART 3 & 4 — BENCHMARKING
# ==========================================

def run_benchmarks():
  ITERATIONS = 100_000
  print("\n\n" + "=" * 60)
  print(f"GUARDIAN PERFORMANCE BENCHMARK ({ITERATIONS:,} iterations)")
  print("=" * 60)

  def measure(name: str, stmt: str, setup: str):
    # Warmup
    timeit.timeit(stmt, setup=setup, globals=globals(), number=1000)
    # Benchmark
    time = timeit.timeit(stmt, setup=setup, globals=globals(), number=ITERATIONS)
    ns_per_op = (time / ITERATIONS) * 1_000_000_000
    return ns_per_op

  results = []

  # 1. Simple Function Call
  setup_simple = ""
  results.append(("Standard Func", measure("Standard", "standard_add(1, 2)", setup_simple)))
  results.append(("Guard Func", measure("Guard", "guard_add(1, 2)", setup_simple)))
  results.append(("DeepGuard Func", measure("DeepGuard", "deepguard_add(1, 2)", setup_simple)))
  results.append(("Beartype Func", measure("Beartype", "beartype_add(1, 2)", setup_simple)))

  # 2. Class Instantiation
  setup_class = ""
  results.append(("Standard/Shield Init", measure("Shield Init", "Cat('Test', 1)", setup_class)))
  results.append(("Pydantic Init", measure("Pydantic Init", "PydanticCat(name='Test', age=1)", setup_class)))

  # 3. Attribute Assignment
  setup_attr_shield = "c = Cat('Test', 1)"
  setup_attr_pydantic = "p = PydanticCat(name='Test', age=1)"
  results.append(("Shield Attr Set", measure("Shield Attr", "c.age = 2", setup_attr_shield)))
  results.append(("Pydantic Attr Set", measure("Pydantic Attr", "p.age = 2", setup_attr_pydantic)))

  # Print Results Table
  print(f"{'Operation / Library':<25} | {'Time (ns / op)':<15} | {'Relative Overhead':<15}")
  print("-" * 60)

  baseline_func = results[0][1]

  for name, ns in results:
    if "Func" in name:
      overhead = f"{(ns / baseline_func):.2f}x" if baseline_func else "1.00x"
    else:
      overhead = "N/A"
    print(f"{name:<25} | {ns:<15.2f} | {overhead:<15}")
  print("=" * 60 + "\n")


def test_run_benchmarks(capsys):
  """
  Hook to run benchmarks via pytest.
  Use `pytest test.py -s` to see the output.
  """
  run_benchmarks()