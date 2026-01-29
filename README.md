# usage 
see `exapmles`

# run exapmles 
```
xmake run --group=examples
```

# run tests
```
xmake run --group=tests
```

# run benchmarks
请先查看 `benchmarks/include/benchmark/config.hpp` 中定义的配置，并根据需要进行修改。
```
# bench one
python scripts/benchmark.py ping_pong_itc
# bench all
python scripts/benchmark.py
```
