from setuptools import setup, Extension

guardian_core = Extension(
    "guardian._guardian_core",
    sources=["src/_guardian_core.c"],
    # We removed the Linux flags that Windows didn't like
    extra_compile_args=["/O2"],
)

setup(
    name="guardian-type-enforcer",
    version="2.0.0",
    ext_modules=[guardian_core],
    packages=["guardian"],
    # THE FIX: Add encoding="utf-8" here
    long_description=open("README.md", encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    # Setuptools now prefers a simple string for license
    license="MIT",
)