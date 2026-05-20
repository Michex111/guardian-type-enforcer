# 🛡️ Guardian — Sub-Microsecond Runtime Type Enforcement & Access Control for Python

Guardian is a blazing-fast runtime type enforcer and data validation framework for Python 3.11+.

Unlike traditional Python validation libraries that rely on slow dictionary wrappers, getattr proxies, or `__dict__` parsing, Guardian is written entirely as a native C-extension. It intercepts assignments directly at the memory level via the CPython API, delivering O(1) validation that outperforms industry standards like Pydantic (Rust).

---

## 🚀 Performance Benchmarks

Guardian is engineered for high-throughput backend APIs and data pipelines where nanosecond performance matters.
*(Benchmarks run for 100,000 iterations against standard Python 3.13 on native Linux)*

| Operation / Library       | Time (ns / op) | Relative Speed |
| ------------------------- | -------------- | -------------- |
| Standard Python Function  | 30.61 ns       | 1.00x          |
| `@guard` Function         | 80.79 ns       | 2.64x          |
| Beartype Function         | 241.10 ns      | 7.88x          |
| Standard / `shield` Init  | 653.75 ns      | N/A            |
| Pydantic v2 Init (Rust)   | 824.64 ns      | N/A            |
| `shield` Attribute Set    | 76.26 ns       | N/A            |
| Pydantic v2 Attribute Set | 194.05 ns      | N/A            |

**Highlights**

* **Function decorators:** `@guard` is ~3× faster than Beartype
* **Class models:** `shield` is ~2.5× faster than Pydantic v2 at attribute assignment

---

## 📦 Installation

Guardian distributes pre-compiled wheels for Linux, macOS, and Windows.

```bash
pip install guardian-type-enforcer
```

---

## 🛠️ Core Features

### 1. Guardian Dataclasses & Domain Validators

Guardian wraps the standard Python library `@dataclass` to provide native C-speed type enforcement with zero namespace pollution.

```python
from guardian.dataclasses import dataclass, validator

@dataclass(frozen=True)
class DatabaseConfig:
    host: str
    port: int

    @validator("host")
    def sanitize_host(cls, value: str):
        return value.strip().lower()

config = DatabaseConfig(host="  LOCALHOST ", port=5432)
print(config.host)  # "localhost"

# ❌ Raises FrozenInstanceError
config.port = 8080
```

---

### 2. shield (C-Native Data Models & Encapsulation)

```python
from guardian.shield import Shield
from guardian._guardian_core import GuardianTypeError, GuardianAccessError

class Account(Shield):
    username: str
    _balance: float
    _total_accounts: int = 0

    def __init__(self, username: str):
        self.username = username
        self._balance = 0.0
        Account._total_accounts += 1

account = Account("Michex")
account.username = "Mike"   # OK
account.username = 123       # Raises GuardianTypeError

print(account._balance)      # Raises GuardianAccessError
Account._total_accounts = 999  # Raises GuardianAccessError
```

---

### 3. @guard (Boundary Type Enforcement)

```python
from typing import Union
from guardian import guard

@guard
def process_sensor_data(payload: dict[str, list[Union[int, float]]]) -> bool:
    return True

process_sensor_data({"sensor_A": [1, 2.5, 3]})
process_sensor_data({"sensor_A": [1, 2.5, "3.0"]})
```

---

### 4. @deepguard (Local State Profiling)

```python
from guardian import deepguard

@deepguard
def calculate_discount(price: float) -> float:
    discount: float = 0.0
    if price < 0:
        discount = "N/A"  # Type error
    return price - discount
```

---

## 🧠 Advanced Usage: The Compiler

Guardian compiles complex type hints into optimized internal representations for C-level evaluation.

---

## 🤝 Contributing

We recommend WSL 2 / Linux for development.

```bash
# Clone repository
git clone https://github.com/yourusername/guardian-type-enforcer.git
cd guardian-type-enforcer

# Create environment
uv venv --python 3.14
source .venv/bin/activate

# Install + build
uv pip install -e .
pytest tests/ -v
```

---

## 📄 License

MIT License — see LICENSE file.

---

# 🔄 Guardian Update Ledger

## ⚡ C-Core Memory & Profiling Optimizations

* Zero-allocation string routing using optimized CPython mappings
* PEP 667 compatibility for Python 3.13+ FrameLocalsProxy
* Read-path acceleration via native CPython attribute lookup

---

## 🏗️ Dataclass Engine & Static Typing

* `guardian.dataclasses.dataclass` fully mirrors stdlib behavior
* Frozen instance compatibility layer added
* `typing.dataclass_transform` integration for IDE support

---

## 🛡️ Access Control & Encapsulation

* Added `@validator` hook system
* Extended frame-walking protection to class-level access

---

## 🛠️ Developer Experience

* Recommended Linux/WSL 2 build pipeline
* Adopted `uv` for ultra-fast environment management
