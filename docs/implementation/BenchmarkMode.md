In order to run latency benchmark without affecting the production app performance, we need to ensure **production builds** carry zero overhead and keep the original packet size, we add a build-time preprocessor macro: `ENABLE_LATENCY_BENCHMARK` that will later be used for controlling packet structure and adding benchmarks (outside the current scope).

### Build Flag Definition (CMake)
Define a formal option in `CMakeLists.txt` to trigger benchmark mode. This option is named `LATENCY_BENCHMARK` and defaults to `OFF` (production mode):

```cmake
# In CMakeLists.txt
option(LATENCY_BENCHMARK "Compile firmware in latency benchmarking mode" OFF)

if(LATENCY_BENCHMARK)
    add_compile_definitions(ENABLE_LATENCY_BENCHMARK=1)
endif()
```

### Build Workflow

The preferred way to compile the firmware in latency benchmarking mode is using the Docker+Makefile workflow:

```bash
make firmware LATENCY_BENCHMARK=ON
```

To compile in production mode (which is the default):

```bash
make firmware LATENCY_BENCHMARK=OFF
```