#!/usr/bin/env bash
#
# Memory Leak Detection Script
# Uses Valgrind to check for memory leaks in all test suites and parsers
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Memory Leak Detection${NC}"
echo -e "${BLUE}========================================${NC}\n"

# Check if valgrind is installed
if ! command -v valgrind &> /dev/null; then
    echo -e "${RED}ERROR: Valgrind is not installed${NC}"
    echo "Install with: sudo apt-get install valgrind"
    echo "Or on NixOS: nix-shell -p valgrind"
    exit 1
fi

# Create results directory
mkdir -p memory_check_results

# Track failures
LEAK_COUNT=0
TOTAL_TESTS=0

# Function to check for leaks in a test
check_test() {
    local test_binary=$1
    local test_name=$(basename "$test_binary")
    local log_file="memory_check_results/${test_name}.log"

    if [ ! -x "$test_binary" ]; then
        echo -e "${YELLOW}  ✗ Not found: $test_binary${NC}"
        return
    fi

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    echo -e "${BLUE}Checking: $test_name${NC}"

    # Run valgrind
    valgrind --leak-check=full \
             --show-leak-kinds=all \
             --track-origins=yes \
             --errors-for-leak-kinds=definite,possible \
             --error-exitcode=1 \
             --log-file="$log_file" \
             "$test_binary" > /dev/null 2>&1

    exit_code=$?

    # Parse results
    definitely_lost=$(grep "definitely lost:" "$log_file" | awk '{print $4}')
    indirectly_lost=$(grep "indirectly lost:" "$log_file" | awk '{print $4}')
    possibly_lost=$(grep "possibly lost:" "$log_file" | awk '{print $4}')
    still_reachable=$(grep "still reachable:" "$log_file" | awk '{print $4}')

    # Check for leaks
    if [ "$definitely_lost" = "0" ] && [ "$indirectly_lost" = "0" ]; then
        echo -e "  ${GREEN}✓ No memory leaks${NC}"
        echo -e "    Definitely lost: ${GREEN}0 bytes${NC}"
        echo -e "    Indirectly lost: ${GREEN}0 bytes${NC}"
        if [ "$possibly_lost" != "0" ]; then
            echo -e "    Possibly lost:   ${YELLOW}$possibly_lost bytes${NC}"
        fi
        if [ "$still_reachable" != "0" ]; then
            echo -e "    Still reachable: ${YELLOW}$still_reachable bytes${NC} (freed at exit)"
        fi
    else
        echo -e "  ${RED}✗ Memory leaks detected${NC}"
        echo -e "    Definitely lost: ${RED}$definitely_lost bytes${NC}"
        echo -e "    Indirectly lost: ${RED}$indirectly_lost bytes${NC}"
        echo -e "    See: $log_file"
        LEAK_COUNT=$((LEAK_COUNT + 1))
    fi

    # Check for errors
    error_count=$(grep "ERROR SUMMARY:" "$log_file" | awk '{print $4}')
    if [ "$error_count" != "0" ]; then
        echo -e "    ${RED}$error_count errors detected${NC}"
        LEAK_COUNT=$((LEAK_COUNT + 1))
    fi

    echo
}

# Check for leaks in a parser
check_parser() {
    local parser_binary=$1
    local test_input=$2
    local parser_name=$(basename "$parser_binary")
    local log_file="memory_check_results/${parser_name}_parse.log"

    if [ ! -x "$parser_binary" ]; then
        echo -e "${YELLOW}  ✗ Not found: $parser_binary${NC}"
        return
    fi

    if [ ! -f "$test_input" ]; then
        echo -e "${YELLOW}  ✗ Test input not found: $test_input${NC}"
        return
    fi

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    echo -e "${BLUE}Checking parser: $parser_name${NC}"

    # Run valgrind
    valgrind --leak-check=full \
             --show-leak-kinds=all \
             --track-origins=yes \
             --errors-for-leak-kinds=definite,possible \
             --error-exitcode=1 \
             --log-file="$log_file" \
             "$parser_binary" < "$test_input" > /dev/null 2>&1

    # Parse results
    definitely_lost=$(grep "definitely lost:" "$log_file" | awk '{print $4}')
    indirectly_lost=$(grep "indirectly lost:" "$log_file" | awk '{print $4}')

    if [ "$definitely_lost" = "0" ] && [ "$indirectly_lost" = "0" ]; then
        echo -e "  ${GREEN}✓ No memory leaks${NC}"
    else
        echo -e "  ${RED}✗ Memory leaks detected${NC}"
        echo -e "    Definitely lost: ${RED}$definitely_lost bytes${NC}"
        echo -e "    Indirectly lost: ${RED}$indirectly_lost bytes${NC}"
        echo -e "    See: $log_file"
        LEAK_COUNT=$((LEAK_COUNT + 1))
    fi

    echo
}

# Test all test binaries
echo -e "${BLUE}=== Testing Core Library ===${NC}\n"

check_test "builddir/tests/test_merkle_tree"
check_test "builddir/tests/test_dependency_resolver"
check_test "builddir/tests/test_parser_composition"
check_test "builddir/tests/test_random_composition"

# Test parsers
echo -e "${BLUE}=== Testing Parsers ===${NC}\n"

# Find or create test input
if [ -d "examples/pg/test" ]; then
    TEST_SQL=$(find examples/pg/test -name "*.sql" -type f | head -1)
elif [ -d "test_data" ]; then
    TEST_SQL="test_data/test.sql"
else
    # Create simple test
    mkdir -p test_data
    cat > test_data/test.sql << 'EOF'
SELECT id, name, email FROM users WHERE active = true ORDER BY name;
EOF
    TEST_SQL="test_data/test.sql"
fi

check_parser "examples/pg/pg_parser" "$TEST_SQL"
check_parser "examples/pg_modular/pg_parser" "$TEST_SQL"

# Test other parsers if they have test inputs
if [ -d "examples/datalog/samples" ]; then
    TEST_DATALOG=$(find examples/datalog/samples -name "*.dl" -type f | head -1)
    if [ -n "$TEST_DATALOG" ]; then
        check_parser "examples/datalog/datalog_parser" "$TEST_DATALOG"
    fi
fi

if [ -d "examples/xpath/tests" ]; then
    TEST_XPATH=$(find examples/xpath/tests -name "*.txt" -type f | head -1)
    if [ -n "$TEST_XPATH" ]; then
        check_parser "examples/xpath/xpath_parser" "$TEST_XPATH"
    fi
fi

if [ -d "examples/xquery/tests" ]; then
    TEST_XQUERY=$(find examples/xquery/tests -name "*.txt" -type f | head -1)
    if [ -n "$TEST_XQUERY" ]; then
        check_parser "examples/xquery/xquery_parser" "$TEST_XQUERY"
    fi
fi

if [ -d "examples/mongodb/tests" ]; then
    TEST_MONGO=$(find examples/mongodb/tests -name "*.txt" -type f | head -1)
    if [ -n "$TEST_MONGO" ]; then
        check_parser "examples/mongodb/mongodb_parser" "$TEST_MONGO"
    fi
fi

# Summary
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Summary${NC}"
echo -e "${BLUE}========================================${NC}\n"

if [ $LEAK_COUNT -eq 0 ]; then
    echo -e "${GREEN}✓ SUCCESS: No memory leaks detected in $TOTAL_TESTS tests${NC}"
    echo -e "\nAll tests passed memory leak detection."
    echo -e "Results saved in: memory_check_results/"
    exit 0
else
    echo -e "${RED}✗ FAILURE: Memory leaks detected in $LEAK_COUNT of $TOTAL_TESTS tests${NC}"
    echo -e "\nPlease review the log files in memory_check_results/"
    echo -e "Fix the leaks and run this script again."
    exit 1
fi
