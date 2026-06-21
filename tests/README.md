# RenAmp Test Suite

This directory contains unit and integration tests for the RenAmp DSP engine.

## Test Structure

```
tests/
├── unit/
│   ├── dsp/
│   │   ├── parameter_smoother_tests.cpp    # Cross-thread parameter smoothing
│   │   ├── limiter_tests.cpp                # Soft-knee peak limiting
│   │   ├── nam_processor_tests.cpp          # NAM model inference
│   │   ├── cabinet_processor_tests.cpp      # IR convolution
│   │   └── dsp_chain_tests.cpp              # DSP chain orchestration
│   └── models/
│       └── nam_model_tests.cpp              # NAM JSON parsing & validation
├── integration/
│   └── dsp_pipeline_tests.cpp               # Full pipeline integration
├── fixtures/
│   └── test_data/
│       ├── models/                          # NAM model fixtures
│       └── irs/                             # IR file fixtures
└── benchmark/
    └── benchmark_main.cpp                    # Performance benchmarks
```

## Building Tests

### Enable Tests in CMake

By default, tests are disabled. Enable them during configuration:

```bash
mkdir build && cd build
cmake -DRENAAMP_BUILD_TESTS=ON ..
make renaamp_tests
```

### With Sanitizers (Recommended for Development)

```bash
cmake -DRENAAMP_BUILD_TESTS=ON \
      -DRENAAMP_ENABLE_SANITIZERS=ON \
      ..
make renaamp_tests
```

## Running Tests

### Run All Tests

```bash
# Using CTest
ctest --verbose

# Or run directly
./renaamp_tests
```

### Run Specific Tests

```bash
# Run only smoother tests
./renaamp_tests "[smoother]"

# Run only limiter tests
./renaamp_tests "[limiter]"

# Run tests with specific tag combination
./renaamp_tests "[nam][dsp]"
```

### Run Specific Test File

```bash
# Run only parameter smoother tests
./renaamp_tests "ParameterSmoother"

# Run only integration tests
./renaamp_tests "[integration]"
```

## Test Categories

### Unit Tests (`tests/unit/`)

- **ParameterSmoother**: Linear interpolation, atomic cross-thread updates, immediate reset
- **Limiter**: Soft-knee gain calculation, peak detection, stereo linking
- **NAMProcessor**: Model loading, LSTM inference, double-buffered swapping
- **CabinetProcessor**: IR loading, convolution (mono/stereo), bypass modes
- **NAMModel**: JSON parsing, architecture validation, weight loading

### Integration Tests (`tests/integration/`)

- **DSP Pipeline**: Full chain processing, component bypass, master gain, limiter integration

## Test Fixtures

Test fixtures are located in `tests/fixtures/test_data/`:

- **Models**: `simple_lstm.nam` - Minimal LSTM model for testing
- **IRs**: `mono_48k.wav`, `stereo_48k.wav` - Impulse responses for cabinet testing

## Coverage

To generate coverage reports (requires gcov/lcov):

```bash
cmake -DRENAAMP_BUILD_TESTS=ON \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="--coverage" \
      ..
make renaamp_tests
./renaamp_tests
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

## Continuous Integration

Tests are designed to run quickly:

- Unit tests: < 100ms each
- Integration tests: < 500ms each
- Total runtime: ~5-10 seconds

## Adding New Tests

1. Create test file in appropriate directory (`tests/unit/` or `tests/integration/`)
2. Use Catch2 BDD style with `SCENARIO/GIVEN/WHEN/THEN`
3. Tag tests appropriately: `[module]`, `[subcategory]`
4. Add fixtures to `tests/fixtures/test_data/` if needed
5. Rebuild with `make renaamp_tests`

## Thread Safety Tests

When running with ThreadSanitizer:

```bash
TSAN_OPTIONS=second_deadlock_stack=1 ./renaamp_tests
```

## Known Issues

- Catch2 headers may not be found in IDE until CMake configures the project
- Some tests use dummy models with zero weights - output validation is limited
- IRModel tests are covered by CabinetProcessor tests (no separate IRModel module exists)

## References

- [Catch2 Documentation](https://github.com/catchorg/Catch2)
- [CMake Documentation](https://cmake.org/documentation/)
