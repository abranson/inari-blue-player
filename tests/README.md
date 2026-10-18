# Pattern engine tests

Run the independent QtTest suite with:

```sh
qmake -o build-tests/Makefile tests/pattern-tests.pro
make -C build-tests
build-tests/pattern-tests
```

The test binary exercises JSON validation, bounds, finite completion,
millisecond fractions represented as microseconds, delayed and staggered
fades, loop boundaries, and every bundled pattern file. It does not require
the hardware backend or a running D-Bus service.
