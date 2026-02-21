import timeit
import gc
from guardian import guard, strictguard
try:
    from beartype import beartype
except ImportError:
    beartype = lambda f: f

def plain(data: list[int], limit: int) -> int:
    return sum(data[:limit])

@guard
def guarded(data: list[int], limit: int) -> int:
    return sum(data[:limit])

@strictguard
def strict_guarded(data: list[int], limit: int) -> int:
    return sum(data[:limit])

@beartype
def bear_guarded(data: list[int], limit: int) -> int:
    return sum(data[:limit])

if __name__ == "__main__":
    gc.disable()
    data = list(range(100))
    n = 1_000_000

    t_plain = timeit.timeit("plain(data, 10)", globals=globals(), number=n)
    t_guard = timeit.timeit("guarded._c_guard(data, 10)", globals=globals(), number=n)
    t_strict = timeit.timeit("strict_guarded._c_guard(data, 10)", globals=globals(), number=n)
    t_bear = timeit.timeit("bear_guarded(data, 10)", globals=globals(), number=n)

    print(f"{'Target':<15} | {'Total (s)':<10} | {'Overhead/call (µs)':<15}")
    print("-" * 45)
    print(f"{'Plain':<15} | {t_plain:<10.4f} | {'-':<15}")
    print(f"{'Guardian C':<15} | {t_guard:<10.4f} | {(t_guard - t_plain) / n * 1e6:<10.3f}")
    print(f"{'StrictGuard C':<15} | {t_strict:<10.4f} | {(t_strict - t_plain) / n * 1e6:<10.3f}")
    print(f"{'Beartype':<15} | {t_bear:<10.4f} | {(t_bear - t_plain) / n * 1e6:<10.3f}")