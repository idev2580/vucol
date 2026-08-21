# Development Request Summary

## User request

Fix the reported `BasicTest.cpp` build failure and warning introduced by the GPU timing tests.

## What to implement

- Prevent GoogleTest's `EXPECT_DOUBLE_EQ` macro from parsing the comma in a `std::chrono::duration` template argument as an extra macro argument.
- Consume the `[[nodiscard]]` result in the invalid timestamp-bit exception test so it does not emit an unused-result warning.

## How to implement

- Compute the converted microsecond value in a local variable before passing it to `EXPECT_DOUBLE_EQ`.
- Wrap the expected-to-throw `timestampDelta()` call in `static_cast<void>`.
- Change only `tests/BasicTest.cpp` and do not refactor unrelated test code.
- Do not compile or execute code in the agent environment, per project instructions.
