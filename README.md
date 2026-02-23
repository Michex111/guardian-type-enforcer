# 🛡️ Guardian — Sub-Microsecond Runtime Type Enforcement & Access Control for Python

Guardian is a blazing-fast runtime type enforcer and data validation framework for Python 3.11+.

Unlike traditional Python validation libraries that rely on slow dictionary wrappers, getattr proxies, or __dict__ parsing, Guardian is written entirely as a native C-extension. It intercepts assignments directly at the memory level via the CPython API, delivering O(1) validation that outperforms industry standards like Pydantic (Rust) and Beartype.

---

## 🚀 Performance Benchmarks

Guardian is engineered for high-throughput backend APIs and data pipelines where nanosecond performance matters.  
(Benchmarks run for 100,000 iterations against standard Python 3.13)

| Operation / Library        | Time (ns / op) | Relative Speed |
|---------------------------|---------------|----------------|
| Standard Python Function  | 30.61 ns      | 1.00x          |
| `@guard` Function         | 80.79 ns      | 2.64x          |
| Beartype Function         | 241.10 ns     | 7.88x          |
| Standard / `shield` Init  | 653.75 ns     | N/A            |
| Pydantic v2 Init (Rust)   | 824.64 ns     | N/A            |
| `shield` Attribute Set    | 76.26 ns      | N/A            |
| Pydantic v2 Attribute Set | 194.05 ns     | N/A            |

**Highlights**
- **Function decorators:** `@guard` is ~3× faster than Beartype  
- **Class models:** `shield` is ~2.5× faster than Pydantic v2 at attribute assignment  

---

## 📦 Installation

Guardian distributes pre-compiled wheels for Linux, macOS, and Windows.

```bash
pip install guardian-type-enforcer
```

---

## 🛠️ Core Features

### 1. `shield` (C-Native Data Models)

The `shield` class is a high-performance alternative to `@dataclass` or `pydantic.BaseModel`.  
By inheriting from `shield`, your class becomes a native C-type (`ShieldBase`). It enforces strict typing on all annotated attributes and provides true protected/private access control.

```python
from guardian.shield import Shield
from guardian._guardian_core import GuardianTypeError, GuardianAccessError


class User(Shield):
  username: str
  age: int
  _internal_id: int  # Attributes starting with '_' are private

  def __init__(self, username: str, age: int):
    self.username = username
    self.age = age
    self._internal_id = 9999


# --- 1. Type Enforcement ---
user = User("Michex", 25)

user.age = 26  # OK
user.age = "twenty"  # ❌ Raises GuardianTypeError

# --- 2. True Private Access Control ---
print(user._internal_id)  # ❌ Raises GuardianAccessError
```

**How it works:**  
`shield` bypasses Python’s `__setattr__`. Memory assignment and type-checking happen directly in C via `tp_setattro` slots, taking less than 80 nanoseconds. Internal access is verified by walking the CPython frame stack to ensure the caller context owns the `self` pointer.

---

### 2. `@guard` (Boundary Type Enforcement)

Use `@guard` to enforce strict input and return types on your critical functions, API endpoints, or calculations.  
Supports deeply nested types like `dict[str, list[int | float]]`.

```python
from typing import Union
from guardian import guard


@guard
def process_sensor_data(payload: dict[str, list[Union[int, float]]]) -> bool:
  return True


# Valid payload
process_sensor_data({"sensor_A": [1, 2.5, 3]})

# Invalid payload
process_sensor_data({"sensor_A": [1, 2.5, "3.0"]})
# ❌ Raises GuardianTypeError

# ignore return
@guard(check_return=False)
def process_data(payload: list[int | str]) -> int:
  return True

process_data([1, "2"])
# ✅ successful 
```

**How it works:**  
Signatures are compiled into C-structs at import time. During runtime, validation is O(1) positional mapped natively in C via the vectorcall protocol (PEP 590).

---

### 3. `@deepguard` (Local State Profiling)

While `@guard` checks inputs and outputs, `@deepguard` acts as a rigorous state machine enforcer. It ensures that local variables inside your function never mutate into unexpected types during execution.

```python
from guardian import deepguard

@deepguard
def calculate_discount(price: float) -> float:
    discount: float = 0.0
    
    if price < 0:
        discount = "N/A"  # ❌ Raises GuardianTypeError when returning
        
    return price - discount
```

**Note:**  
`@deepguard` utilizes `PyEval_SetProfile` to intercept the local dictionary state upon function return. Because it hooks into interpreter profiling, it carries a ~43µs overhead and should be reserved for high-stakes business logic. 

Because of its deep tracing nature, `deepguard` incurs performance overhead 
  and is best utilized as a strict development, debugging, or auditing tool 
  rather than in high-performance production loops.

---

## 🧠 Advanced Usage: The Compiler

Guardian automatically compiles complex type hints into flat, recursive integer tuples that the C-core switch statements can evaluate instantly.

**Supported types include:**
- Primitives (`int`, `str`, `float`, `bool`)
- Data structures (`list`, `dict`, `set`, `tuple`)
- Typing constructs (`Union`, `Optional`, `Any`, `Literal`)
- Deeply nested configurations (`dict[str, tuple[int, ...]]`)

Primitives use branch-predicted fast paths (`PyLong_CheckExact`, `PyUnicode_CheckExact`) to skip `isinstance()` overhead entirely.

---

## 🤝 Contributing

Contributions, issues, and feature requests are welcome.

```bash
git clone https://github.com/yourusername/guardian-type-enforcer.git
pip install -e .
pytest tests/test.py -v
```

---

## 📄 License

This project is licensed under the MIT License.  
See the `LICENSE` file for details.