# Guardian 🛡️

**Maximum performance runtime type enforcement for Python 3.10+**

Guardian is a C-optimized runtime type checker that ensures your Python functions receive and return exactly the types they expect. It operates at two levels: blazing-fast function boundary enforcement, and strict local-variable execution tracing.

Built directly on the CPython C-API using the Vectorcall protocol, Guardian delivers microsecond-level overhead.



## Features
* **`@guard`**: Enforces parameter and return types at the function boundary (~0.67µs overhead).
* **`@strictguard`**: Enforces boundaries *and* locks local variables to their annotated or initially assigned types dynamically (~13µs overhead).
* **Comprehensive Typing Support**: Supports `Union` (`|`), `Literal`, `Annotated`, `list[T]`, `dict[K, V]`, `set[T]`, `tuple[T, ...]`, Custom Classes, and Forward References.
* **C-Level Performance**: Pre-compiled type-check trees and zero Python-level lambda recursion.

## Installation

```bash
pip install guardian
(Note: Guardian utilizes a C extension. If installing from source on Windows, you will need the Visual Studio C++ Build Tools installed.)

Usage1. Function Boundary Enforcement (@guard)
Use @guard for maximum-speed input/output validation.Python

from guardian import guard

@guard
def process_data(data: list[int], limit: int | None = None) -> int:
    if limit is None:
        return sum(data)
    return sum(data[:limit])

process_data([1, 2, 3], limit=2)  # OK
process_data([1, "2", 3])         # GuardianTypeError: Parameter 'data' expected list[int]
2. Strict Internal Execution Tracing (@strictguard)
Use @strictguard for mathematically complex or security-sensitive functions where local variable mutation bugs would be catastrophic.Python

from guardian import strictguard

@strictguard
def strict_math(x: int) -> int:
    y = 10        # 'y' is dynamically locked to 'int'
    y = 20        # OK
    y = "bad"     # GuardianTypeError: Variable 'y' expected int, got str
    return x + y
```



## Performance BenchmarkMeasured on Python 3.13 (1,000,000 iterations)

| TargetTotal | Time (s) | Overhead/call (µs) |
|-------------|----------|--------------------|
| Plain Function | 0.1514 | – |
| Guardian C (@guard) | 0.8278 | ~0.67 µs |
| StrictGuard C (@strictguard) | 13.1459 | ~13.0 µs |