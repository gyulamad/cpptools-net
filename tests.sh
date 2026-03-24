BUILD_COMMAND="./builder tests/tests.cpp --mode=strict,test" && $BUILD_COMMAND && echo "Built [OK]" && \
timeout 10 bash -c "$BUILD_COMMAND --run" || echo "[TIMEOUT ERROR]"
    # ( && echo "[OK] - Tests passes" || echo "[ERROR] ($?)")