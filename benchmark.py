import timeit
from typing import Union, List, Dict
from pydantic import BaseModel
from beartype import beartype

from guardian.guard_set import guard, deepguard
from guardian.shield import Shield


# ==========================================
# BENCHMARK TARGETS
# ==========================================

def standard_add(a: int, b: int) -> int:
  return a + b


@guard
def guard_add(a: int, b: int) -> int:
  return a + b


@deepguard
def deepguard_add(a: int, b: int) -> int:
  return a + b


@beartype
def beartype_add(a: int, b: int) -> int:
  return a + b


class ShieldCat(Shield):
  name: str
  age: int

  def __init__(self, name: str, age: int):
    self.name = name
    self.age = age


class PydanticCat(BaseModel):
  name: str
  age: int


# ==========================================
# BENCHMARK RUNNER
# ==========================================

def run_benchmarks(iterations: int = 100_000):
  print("\n" + "=" * 65)
  print(f"GUARDIAN PERFORMANCE BENCHMARK ({iterations:,} iterations)")
  print("=" * 65)

  def measure(stmt: str, setup: str = "pass") -> float:
    # Warmup loop to prime the CPU cache and JIT (if applicable)
    timeit.timeit(stmt, setup=setup, globals=globals(), number=1000)
    # Actual measurement
    total_time = timeit.timeit(stmt, setup=setup, globals=globals(), number=iterations)
    # Convert total time to nanoseconds per operation
    return (total_time / iterations) * 1_000_000_000

  results = []

  # 1. Simple Function Call (Overhead Test)
  results.append(("Standard Func", measure("standard_add(1, 2)"), "func"))
  results.append(("Guard Func", measure("guard_add(1, 2)"), "func"))
  results.append(("DeepGuard Func", measure("deepguard_add(1, 2)"), "func"))
  results.append(("Beartype Func", measure("beartype_add(1, 2)"), "func"))

  # 2. Class Instantiation
  results.append(("Standard/Shield Init", measure("ShieldCat('Luna', 3)"), "init"))
  results.append(("Pydantic Init", measure("PydanticCat(name='Luna', age=3)"), "init"))

  # 3. Attribute Assignment (Setup phase creates the object so we ONLY measure the assignment)
  setup_shield_attr = "c = ShieldCat('Luna', 3)"
  setup_pydantic_attr = "p = PydanticCat(name='Luna', age=3)"

  results.append(("Shield Attr Set", measure("c.age = 4", setup=setup_shield_attr), "attr"))
  results.append(("Pydantic Attr Set", measure("p.age = 4", setup=setup_pydantic_attr), "attr"))

  # ==========================================
  # PRINT RESULTS TABLE
  # ==========================================

  print(f"{'Operation / Library':<25} | {'Time (ns / op)':<15} | {'Relative Overhead':<15}")
  print("-" * 65)

  # Use standard python function as the baseline for 1.00x overhead
  baseline_func = results[0][1]

  for name, ns, category in results:
    if category == "func":
      overhead = f"{(ns / baseline_func):.2f}x"
    else:
      overhead = "N/A"

    print(f"{name:<25} | {ns:<15.2f} | {overhead:<15}")

  print("=" * 65 + "\n")


if __name__ == "__main__":
  # You can increase this to 1_000_000 if you want an even more stable average
  run_benchmarks(iterations=100_000)