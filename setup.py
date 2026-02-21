import sys
from setuptools import setup, Extension

# 1. Define platform-specific compiler arguments
if sys.platform == "win32":
    compile_args = ["/O2"]
else:
    # Mac (clang) and Linux (gcc) use these
    compile_args = ["-O3", "-Wno-unused-function"]

# 2. Define the Extension
# The name MUST be "guardian._guardian_core" so it lands in the right folder
guardian_core = Extension(
    "guardian._guardian_core",
    sources=["src/_guardian_core.c"],
    extra_compile_args=compile_args,
)

setup(
    name="guardian-type-enforcer",
    version="2.0.6", # Increment to 2.0.4 to bypass PyPI's 'no overwrite' rule
    ext_modules=[guardian_core],
    packages=["guardian"],
    long_description=open("README.md", encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    license="MIT",
    python_requires=">=3.10",
)