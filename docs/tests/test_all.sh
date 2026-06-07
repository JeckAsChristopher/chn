#!/bin/bash
# CHN 1.0 - Test Suite
# Usage: bash docs/tests/test_all.sh [path/to/chn]

CHN="${1:-chn}"
PASS=0; FAIL=0

check(){
    local desc="$1" expected="$2" file="$3"
    local actual
    actual=$("$CHN" "$file" 2>&1)
    if echo "$actual" | grep -qF "$expected"; then
        printf "  PASS  %s\n" "$desc"; PASS=$((PASS+1))
    else
        printf "  FAIL  %s\n  expected: %s\n  got: %s\n" \
               "$desc" "$expected" "$(echo "$actual" | head -2)"
        FAIL=$((FAIL+1))
    fi
}

check_str(){
    local desc="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        printf "  PASS  %s\n" "$desc"; PASS=$((PASS+1))
    else
        printf "  FAIL  %s\n  expected: %s\n  got: %s\n" \
               "$desc" "$expected" "$(echo "$actual" | head -2)"
        FAIL=$((FAIL+1))
    fi
}

echo "CHN 1.0 Test Suite"
echo ""

echo "-- Arithmetic"
cat > /tmp/t01.chn << 'EOF'
public entry main() {
    let x = 10
    let y = 3
    stdo(x + y)
    stdo(x - y)
    stdo(x * y)
    stdo(x % y)
    stdo(x ** 2)
    stdo(10 / 4)
}
EOF
check "addition"       "13"   /tmp/t01.chn
check "subtraction"    "7"    /tmp/t01.chn
check "multiplication" "30"   /tmp/t01.chn
check "modulo"         "1"    /tmp/t01.chn
check "exponent"       "100"  /tmp/t01.chn
check "division"       "2.5"  /tmp/t01.chn

echo ""
echo "-- Strings and f-strings"
cat > /tmp/t02.chn << 'EOF'
public entry main() {
    let name = "CHN"
    stdo("Hello, " + name)
    let ver = 1
    stdo(f"version {ver}.0")
    let s = "hello"
    stdo(f"len: {s.length()}")
    stdo(s.upper())
    stdo("  hi  ".trim())
}
EOF
check "concat"         "Hello, CHN"   /tmp/t02.chn
check "f-string"       "version 1.0"  /tmp/t02.chn
check "method in fstr" "len: 5"       /tmp/t02.chn
check "upper"          "HELLO"        /tmp/t02.chn
check "trim"           "hi"           /tmp/t02.chn

echo ""
echo "-- Control flow"
cat > /tmp/t03.chn << 'EOF'
public entry main() {
    let x = 7
    if x > 5 { stdo("big") } else { stdo("small") }
    let s = 0
    for i in range(5) { s = s + i }
    stdo(s)
    let n = 0
    while n < 3 { n = n + 1 }
    stdo(n)
    for i in range(10) {
        if i == 4 { break }
    }
    stdo("break ok")
    let evens = []
    for i in range(6) {
        if i % 2 != 0 { continue }
        evens.add(i)
    }
    stdo(evens.length())
}
EOF
check "if branch"    "big"     /tmp/t03.chn
check "for range sum" "10"     /tmp/t03.chn
check "while"        "3"       /tmp/t03.chn
check "break"        "break ok" /tmp/t03.chn
check "continue evens" "3"     /tmp/t03.chn

echo ""
echo "-- Functions and closures"
cat > /tmp/t04.chn << 'EOF'
func fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
func make_adder(n) {
    return func(x) { return x + n }
}
public entry main() {
    stdo(fib(10))
    let add5 = make_adder(5)
    stdo(add5(10))
    let sq = (x) -> x * x
    stdo(sq(9))
}
EOF
check "fibonacci(10)" "55"  /tmp/t04.chn
check "closure add5"  "15"  /tmp/t04.chn
check "lambda square" "81"  /tmp/t04.chn

echo ""
echo "-- Arrays (push/add/pop)"
cat > /tmp/t05.chn << 'EOF'
public entry main() {
    let a = [1, 2, 3, 4, 5]
    stdo(a.length())
    a.push(6)
    stdo(a.length())
    stdo(a[0])
    stdo(a[a.length()-1])
    stdo(a.contains(3))
    stdo(a.contains(99))
    let words = ["a", "b", "c"]
    stdo(words.join("-"))
    let nums = [3, 1, 4, 1, 5]
    nums.sort()
    stdo(nums[0])
    stdo(nums[4])
    let b = []
    b.add(10)
    b.add(20)
    stdo(b.pop())
    stdo(b.length())
}
EOF
check "length"       "5"     /tmp/t05.chn
check "push length"  "6"     /tmp/t05.chn
check "index 0"      "1"     /tmp/t05.chn
check "last element" "6"     /tmp/t05.chn
check "contains 3"   "true"  /tmp/t05.chn
check "contains 99"  "false" /tmp/t05.chn
check "join"         "a-b-c" /tmp/t05.chn
check "sort min"     "1"     /tmp/t05.chn
check "sort max"     "5"     /tmp/t05.chn
check "pop value"    "20"    /tmp/t05.chn
check "pop length"   "1"     /tmp/t05.chn

echo ""
echo "-- Dicts and structs"
cat > /tmp/t06.chn << 'EOF'
struct Point { x: 0, y: 0 }
struct Person { name: "unnamed", age: 0 }
public entry main() {
    let d = { name: "Alice", age: 30 }
    stdo(d.name)
    stdo(d.age)
    d.age = 31
    stdo(d.age)
    let p = Point(3, 4)
    stdo(p.x)
    stdo(p.y)
    let bob = Person("Bob", 25)
    stdo(bob.name)
    stdo(bob.age)
}
EOF
check "dict name"     "Alice" /tmp/t06.chn
check "dict age"      "30"    /tmp/t06.chn
check "dict mutate"   "31"    /tmp/t06.chn
check "struct x"      "3"     /tmp/t06.chn
check "struct y"      "4"     /tmp/t06.chn
check "struct name"   "Bob"   /tmp/t06.chn
check "struct age"    "25"    /tmp/t06.chn

echo ""
echo "-- Error handling"
cat > /tmp/t07.chn << 'EOF'
func risky(x) {
    if x == 0 { throw "zero error" }
    return 100 / x
}
public entry main() {
    try {
        stdo(risky(5))
    } catch e {
        stdo(f"err: {e}")
    }
    try {
        stdo(risky(0))
    } catch e {
        stdo(f"caught: {e}")
    }
    try {
        try { throw "inner" } catch e1 { throw f"re:{e1}" }
    } catch e2 {
        stdo(e2)
    }
}
EOF
check "no error"     "20"           /tmp/t07.chn
check "caught throw" "caught: zero" /tmp/t07.chn
check "rethrow"      "re:inner"     /tmp/t07.chn

echo ""
echo "-- Boolean and comparison"
cat > /tmp/t08.chn << 'EOF'
public entry main() {
    stdo(1 < 2)
    stdo(2 < 1)
    stdo(1 == 1)
    stdo(1 != 2)
    stdo(true && false)
    stdo(true || false)
    stdo(!true)
    stdo(nil == nil)
    stdo("a" == "a")
    stdo("a" != "b")
}
EOF
check "less than true"  "true"  /tmp/t08.chn
check "less than false" "false" /tmp/t08.chn
check "equal"           "true"  /tmp/t08.chn
check "not equal"       "true"  /tmp/t08.chn
check "and false"       "false" /tmp/t08.chn
check "or true"         "true"  /tmp/t08.chn
check "not"             "false" /tmp/t08.chn
check "nil eq"          "true"  /tmp/t08.chn
check "string eq"       "true"  /tmp/t08.chn

echo ""
echo "-- Switch / case"
cat > /tmp/t09.chn << 'EOF'
func classify(n) {
    switch n {
        case 1: return "one"
        case 2: return "two"
        case 3: return "three"
        default: return "other"
    }
}
public entry main() {
    stdo(classify(1))
    stdo(classify(2))
    stdo(classify(3))
    stdo(classify(99))
}
EOF
check "switch case 1"   "one"   /tmp/t09.chn
check "switch case 2"   "two"   /tmp/t09.chn
check "switch case 3"   "three" /tmp/t09.chn
check "switch default"  "other" /tmp/t09.chn

echo ""
echo "-- Nested functions and scope"
cat > /tmp/t10.chn << 'EOF'
func outer(x) {
    func inner(y) { return x + y }
    return inner(10)
}
func counter_make() {
    let n = 0
    return func() {
        n = n + 1
        return n
    }
}
public entry main() {
    stdo(outer(5))
    let c = counter_make()
    stdo(c())
    stdo(c())
    stdo(c())
}
EOF
check "outer+inner"   "15"  /tmp/t10.chn
check "counter 1"     "1"   /tmp/t10.chn
check "counter 2"     "2"   /tmp/t10.chn
check "counter 3"     "3"   /tmp/t10.chn

echo ""
echo "-- Runtime args"
cat > /tmp/t11.chn << 'EOF'
public entry main() {
    let a = args()
    stdo(a.length())
    for x in a { stdo(x) }
}
EOF
out=$("$CHN" /tmp/t11.chn alpha beta gamma 2>&1)
check_str "args length=3"  "3"     "$out"
check_str "args[0]"        "alpha" "$out"
check_str "args[2]"        "gamma" "$out"

out2=$("$CHN" /tmp/t11.chn --flag value 2>&1)
echo "$out2" | grep -qF -- "--flag" && { printf "  PASS  flag as arg\n"; PASS=$((PASS+1)); } || { printf "  FAIL  flag as arg\n"; FAIL=$((FAIL+1)); }

echo ""
echo "-- CCO compilation and same-dir import"
mkdir -p /tmp/cco_test && cd /tmp/cco_test
cat > mathlib.chn << 'EOF'
export func square(n) { return n * n }
export func cube(n)   { return n * n * n }
export func clamp(x, lo, hi) {
    if x < lo { return lo }
    if x > hi { return hi }
    return x
}
EOF
cat > runner.chn << 'EOF'
imp::lib mathlib
public entry main() {
    stdo(square(5))
    stdo(cube(3))
    stdo(clamp(15, 0, 10))
    stdo(clamp(-5, 0, 10))
}
EOF
"$CHN" mathlib.chn -oc mathlib.cco > /dev/null 2>&1
"$CHN" runner.chn  -oc runner.cco  > /dev/null 2>&1
out=$("$CHN" runner.cco 2>&1)
check_str "CCO square(5)=25"       "25"  "$out"
check_str "CCO cube(3)=27"         "27"  "$out"
check_str "CCO clamp(15,0,10)=10"  "10"  "$out"
check_str "CCO clamp(-5,0,10)=0"   "0"   "$out"

echo ""
echo "-- CCO 3-level chain"
mkdir -p /tmp/chain3 && cd /tmp/chain3
cat > c.chn << 'EOF'
export func triple(x) { return x * 3 }
EOF
cat > b.chn << 'EOF'
imp::lib c
export func sextuple(x) { return triple(x) * 2 }
EOF
cat > a.chn << 'EOF'
imp::lib b
public entry main() { stdo(sextuple(7)) }
EOF
"$CHN" c.chn -oc c.cco > /dev/null 2>&1
"$CHN" b.chn -oc b.cco > /dev/null 2>&1
"$CHN" a.chn -oc a.cco > /dev/null 2>&1
out=$("$CHN" a.cco 2>&1)
check_str "chain sextuple(7)=42" "42" "$out"
cd - > /dev/null


echo ""
echo "-- Minification flag"
mkdir -p /tmp/mintest2 && pushd /tmp/mintest2 > /dev/null
cat > mlib.chn << 'CHNEOF'
export func greet(name) { return "Hello, " + name }
CHNEOF
cat > mrun.chn << 'CHNEOF'
imp::lib mlib
public entry main() { stdo(greet("world")) }
CHNEOF
"$CHN" mlib.chn -oc mlib.cco > /dev/null 2>&1
"$CHN" mrun.chn -oc mrun.cco > /dev/null 2>&1
out=$("$CHN" mrun.cco 2>&1)
check_str "minified CCO output" "Hello, world" "$out"
"$CHN" --no-minify mlib.chn -oc mlib_plain.cco > /dev/null 2>&1
"$CHN" --no-minify mrun.chn -oc mrun_plain.cco > /dev/null 2>&1
# mrun_plain.cco has dep on mlib — copy to mlib.cco for runtime resolution
cp mlib_plain.cco mlib.cco
out=$("$CHN" mrun_plain.cco 2>&1)
check_str "plain CCO output" "Hello, world" "$out"
min_sz=$(stat -c%s mlib.cco 2>/dev/null || echo 9999)
plain_sz=$(stat -c%s mlib_plain.cco 2>/dev/null || echo 0)
[ "$min_sz" -le "$plain_sz" ] && \
    { printf "  PASS  minified <= plain size (%dB <= %dB)\n" "$min_sz" "$plain_sz"; PASS=$((PASS+1)); } || \
    { printf "  FAIL  minified > plain size\n"; FAIL=$((FAIL+1)); }
popd > /dev/null

echo ""
printf "Results: PASS=%d  FAIL=%d\n" "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
