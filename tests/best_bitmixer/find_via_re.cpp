
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cfloat>
#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <spawn.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using i64 = int64_t;
using u64 = uint64_t;
using rpn_seq_width = u64;
using rpn_seq_vec = std::vector<rpn_seq_width>;
using template_expr_vec = std::vector<u64>;

constexpr u64 n = 4;  // num total operators
constexpr u64 k = 3;  // num operators per expression

constexpr int num_ops = 8;
const char *op_strs[] = { "*", "^", "<<", ">>", "(", ")", "x", "A" };
enum { MUL, XOR, LSH, RSH, LPAREN, RPAREN, X_VAR, C_VAR };

#define C_VAR_CH 'A'
#define OPERATOR 0
#define OPERAND 1
#define SHIFT_OP(x) ((x) == LSH || (x) == RSH)
#define  ASSOCIATIVE(x) ((x) == MUL || (x) == XOR)
#define IS_OPERAND(x) ((x) == C_VAR || (x) == X_VAR)

void pr(rpn_seq_width p)
{
    u64 bits = 2*k+1;

        for (i64 i = 0; i < bits; ++i)
        {
            if (p >> i & 1) std::cout << "A ";
            else                  std::cout << "o ";
        }
        std::cout << "\n";
}

void pr3(rpn_seq_width p)
{
    for (i64 i = 0; i < 2*k+1; ++i)
    {
        std::cout << op_strs[p >> 3*i & 7] << " ";
    }
    std::cout << p << "\n";
}

std::string pr3str(rpn_seq_width p)
{
    std::string s;
    for (i64 i = 0; i < 2*k+1; ++i)
        s += std::string(op_strs[p >> 3*i & 7]) + " ";
    return s;
}

void pr3r(u64 p)
{
    int bits = 64 - __builtin_clzll(p);
    for (int shift = 0; shift < bits; shift += 3)
        std::cout << op_strs[p >> shift & 7];
    std::cout << "\n";
}

void pri64_rpn_seq(const rpn_seq_vec &v)
{
    for (auto pattern : v)
    {
        pr(pattern);
    }
}

rpn_seq_vec gen_rpn_seq(std::unordered_map<u64, rpn_seq_vec> &map, 
        u64 total_operators, u64 used_operators, u64 used_operands, u64 depth)
{
    // max(depth) = k+1, max(used_ops) = k, max(used_vars) = k+1, k <= 0xfffff
    u64 key = (u64)depth << 40 | (u64)used_operators << 20 | used_operands;
    if (map.count(key)) return map[key];

    rpn_seq_vec res;

    if (used_operators == total_operators)
    {
        u64 remaining = (total_operators + 1) - used_operands;
        res.push_back((1ull << remaining) - 1);
        map[key] = std::move(res);
        return map[key];
    }

    if (used_operands < total_operators + 1)
    {
        auto v = gen_rpn_seq(map, total_operators, used_operators, 
                                used_operands + 1, depth + 1);
        for (auto s : v) res.push_back(s << 1 | OPERAND);
    }

    if (depth >= 2)
    {
        auto v = gen_rpn_seq(map, total_operators, used_operators + 1,
                                used_operands, depth - 1);
        for (auto s : v) res.push_back(s << 1 | OPERATOR);
    }

    map[key] = std::move(res);
    return map[key];
}

rpn_seq_vec gen_rpn_seq(u64 total_operators)
{
    static std::unordered_map<u64, rpn_seq_vec> map;
    return gen_rpn_seq(map, total_operators, 0, 0, 0);
}

struct Node
{
    Node(i64 op) : op(op) {}
    Node(Node &&n) : op(n.op), children(std::move(n.children)) {}
    Node(const Node &n) : op(n.op), children(n.children) {}
    Node &operator=(const Node &n) { op = n.op; children = n.children; return *this; }
    Node(Node &&l, Node &&r, i64 op)
        : op(op), children({ std::move(l), std::move(r) }) {}

    i64 op;
    std::vector<Node> children;

    void join_or_push_back(Node &&lhs, Node &&rhs)
    {
        assert(ASSOCIATIVE(op));
        auto fn = [this](Node &&n)
        {
            if (n.op != op)
                children.emplace_back(std::move(n));
            else
                for (auto &c : n.children)
                    children.emplace_back(std::move(c));
        };

        fn(std::move(lhs)); fn(std::move(rhs));
        std::sort(children.begin(), children.end(), 
        [](const Node &a, const Node &b)
        {
            return a.op < b.op;
        });
    }

    static std::string to_string(const Node &n)
    {
        // if it was called with ordered=true, any C_VAR has n.op > C_VAR
        // NOTE imposes a limit on k to 54-3, but that's massive, no use 'x'
        static_assert(k <= 26, "why");
        if (n.children.empty())
            return n.op <= C_VAR ? op_strs[n.op] : 
                std::string(1, C_VAR_CH + (n.op - C_VAR - 1));

        std::string s = "(";
        s += op_strs[n.op];
        for (i64 i = 0; i < n.children.size(); ++i)
            s += to_string(n.children[i]);
        s += ")";

        return s;
    }
    std::string to_string() { return to_string(*this); }

    // TODO join these two together
    // NOTE: no ordered version yet (A0,A1...)
    // Since not ordered yet, treat parentheses as 
    // lparen - placeholder1
    // rparen - placeholder2
    // max 64 / 3 = 21 tokens
    struct _P { u64 h; int num_tok; };
    static _P to_u64(const Node &n, bool first)
    {
        if (n.children.empty())
            return { (u64)n.op, 1 };

        int num_tok = 1;
        u64 h = first ? n.op : n.op << 3 * num_tok++ | LPAREN;
        for (int i = 0; i < n.children.size(); ++i)
        {
            _P p = to_u64(n.children[i], false);
            h |= p.h << num_tok * 3;
            num_tok += p.num_tok;
        }

        h |= first ? 0 : (u64)RPAREN << 3 * num_tok++;
        return { h, num_tok };
    }
    u64 to_u64() { return to_u64(*this, true).h; }
};


// TODO verify algorithm and i64roduce hashing with minimum collisions
template <typename Mode, bool ordered = false>
auto normalize_sequence(rpn_seq_width pattern)
{
    // Not enough bits to pack
    static_assert(!(std::is_same_v<Mode, u64> && ordered), "ordered=false with u64");
    u64 bits = 2*k+1, c_var_idx = 0;
    std::vector<Node> st;

    for (i64 i = 0; i < bits; ++i)
    {
        i64 op = pattern >> 3*i & 7;
        if (IS_OPERAND(op))
        {
            st.emplace_back(op);
                //st.emplace_back(op == X_VAR ? op : op + ++c_var_idx);
        }
        else
        {
            if (st.size() < 2) { pr3(pattern); assert(0); }
            Node rhs = std::move(st.back());
            st.pop_back();
            Node lhs = std::move(st.back());
            st.pop_back();

            if constexpr (ordered) { if (rhs.op == C_VAR) rhs.op += ++c_var_idx; }
            if constexpr (ordered) { if (lhs.op == C_VAR) lhs.op += ++c_var_idx; }

            if (!ASSOCIATIVE(op))
                st.emplace_back(std::move(lhs), std::move(rhs), op);
            else
            {
                st.emplace_back(op);
                st.back().join_or_push_back(std::move(lhs), std::move(rhs));
            }
        }
    }
    if constexpr (std::is_same_v<Mode, u64>)
        return st[0].to_u64();
    else if constexpr (std::is_same_v<Mode, std::string>)
        return st[0].to_string();
    else
        static_assert(0, "u64 or string");
}

template_expr_vec gen_template_expr(u64 total_operators)
{
    rpn_seq_vec patterns = gen_rpn_seq(total_operators);

    std::cout << "About to explore " << std::pow(n, k) * patterns.size() << " templates\n";

    std::unordered_set<u64> normalized_seqs;
    std::unordered_set<u64> seen_hashes;
    template_expr_vec exprs;
    u64 odometer[k] {};

    std::random_device rd;
    std::mt19937 gen(rd());
    //std::mt19937 gen(0);
    std::uniform_int_distribution<u64> distrib;
    constexpr i64 n_is = 3;
    u64 nums[n_is][k+1];
    for (i64 i = 0; i < n_is;  ++i)
        for (i64 j = 0; j < k+1; ++j)
            nums[i][j] = distrib(gen);

    while (true)
    {
        for (auto pattern : patterns)
        {
            i64 st[k+1];
            u64 st2[n_is][k+1], seq = 0;
            u64 op_idx = 0, num_bits = 2*k+1, sp = 0, prev_op = -1, valid = 1, num_ptr = 0;

            for (u64 shift = 0; shift < num_bits; ++shift)
            {
                u64 op = pattern >> shift & 1;
                if (op == OPERAND)
                {
                    seq |= (u64)C_VAR << (3 * shift);
                    st[sp++] = C_VAR;
                }
                else
                {
                    i64 rhs = st[--sp];
                    i64 lhs = st[--sp];
                    // prevent X >> (X * X) and (X >> X) >> X, or (X >> X) << X -- useless in bitmixing
                    if (SHIFT_OP(odometer[op_idx]) && (rhs != C_VAR || SHIFT_OP(prev_op)))
                    {
                        valid = false; 
                        break;
                    }

                    using fns = u64(*)(u64, u64);
                    fns fs[] = { [](u64 x,u64 y){return x*y;},
                                 [](u64 x,u64 y){return x^y;},
                                 [](u64 x,u64 sh){sh = (sh&63)+0; return (x<<sh)|(x>>(64-sh));},
                                 [](u64 x,u64 sh){sh = (sh&63)+0; return (x>>sh)|(x<<(64-sh));}};

                    for (i64 i = 0; i < n_is; ++i)
                    {
                        u64 l = lhs == C_VAR ? nums[i][num_ptr] : st2[i][sp]; // Cannot advance num_ptr yet
                        u64 r = rhs == C_VAR ? nums[i][num_ptr + (lhs == C_VAR ? 1 : 0)] : st2[i][sp+1];
                        st2[i][sp] = fs[odometer[op_idx]](l, r);
                    }
                    if (lhs == C_VAR) num_ptr++;
                    if (rhs == C_VAR) num_ptr++;

                    st[sp++] = -1;
                    prev_op = odometer[op_idx];
                    seq |= (u64)odometer[op_idx++] << (3 * shift);
                }
            }

            if (valid)
            {
                /*
                i64 cnt = 0;
                for (i64 i = 0; i < n_is; ++i) 
                    if ((cnt = seen_hashes.count(st2[i][0])))
                        break;
                if (!cnt) */
                auto mix = [](u64 h1, u64 h2)
                {
                    const u64 kMul = 0x9ddfea08eb382d69ULL;
                    u64 a = (h1 ^ h2) * kMul;
                    a ^= (a >> 47);
                    u64 b = (h2 ^ a) * kMul;
                    b ^= (b >> 47);
                    return b * kMul;
                };
                u64 h = 0;
                for (i64 i = 0; i < n_is; ++i)
                    h = mix(st2[i][0], h);

                if (!seen_hashes.count(h))
                {
                    // Fast but fails to detect: X X * X X >> * and X X X X >> * *
                    // (X * X) * (X >> X)
                    // X * (X * (X >> X))
                    u64 ss = normalize_sequence<u64>(seq);
                    if (!normalized_seqs.count(ss))
                    {
                        //std::cout << ss << "\n";
                        //pr3(seq);
                        exprs.push_back(seq);
                        seen_hashes.insert(h);
                        normalized_seqs.insert(ss);
                        //for (i64 i = 0; i < n_is; ++i)
                            //seen_hashes.insert(st2[i][0]);
                    }
                }
            }
        }

        i64 i = k - 1;
        while (i >= 0)
        {
            odometer[i]++;
            if (odometer[i] < n) break;
            odometer[i] = 0;
            i--;
        }
        if (i < 0) break;
    }

    std::cout << "RPN Templates Pruned size " << exprs.size() << "\n";
    return exprs;
}


// TODO struct which has a bit iterator based on what kind of packing
// 3-bit pack, 1-bit pack, what each packed bit-token means

// TODO atomic pool
// Atomic Trie
struct ATrieNode
{
    std::atomic<bool> terminal { false }; // a sequence ends here
    // 0=MUL, XOR, LSH, RSH, LPAREN, RPAREN, X_VAR, C_VAR
    std::atomic<ATrieNode *> children[num_ops] = { nullptr };
};

bool atrie_exists(ATrieNode *head, u64 expr)
{
    ATrieNode *cur = head;
    int bits = 64 - __builtin_clzll(expr);
    for (int shift = 0; shift < bits; shift += 3)
    {
        int op_idx = expr >> shift & 7;
        cur = cur->children[op_idx].load(std::memory_order_acquire);
        if (cur == nullptr) return false; 
    }
    return cur->terminal.load(std::memory_order_acquire);
}

void atrie_insert(ATrieNode *head, u64 expr)
{
    ATrieNode *cur = head;
    int bits = 64 - __builtin_clzll(expr);
    for (int shift = 0; shift < bits; shift += 3)
    {
        int op_idx = expr >> shift & 7;
        ATrieNode *next = cur->children[op_idx].load(std::memory_order_acquire);

        if (!next)
        {
            ATrieNode *nn = new ATrieNode();
            ATrieNode *expected = nullptr;
            if (cur->children[op_idx].compare_exchange_strong(expected, nn, 
                                                        std::memory_order_release,
                                                        std::memory_order_acquire))
                next = nn;
            else
            {
                delete nn;
                next = expected;
            }
        }
        cur = next;
    }
    cur->terminal.store(true, std::memory_order_release);
}

// TODO create a template function that abstracts this process
// we're doing the same as above
static_assert(k <= 32, "y so big, change u64->u64");
inline u64 judge_varX_placement(u64 template_expr, u64 bit_pattern)
{
    // parentheses are important to denote groups!!!
    // TODO change to atomic trie
    //static std::unordered_set<u64> seen_hashes;
    static ATrieNode head;
    u64 st[k+1], sp = 0, valid = true;
    u64 possible_expr = 0, var_idx = k;
    for (i64 shift = 0; shift < 2*k+1; ++shift)
    {
        i64 op = (template_expr >> (3 * shift)) & 7;
        if (IS_OPERAND(op))
        {
            bool is_x = (bit_pattern >> var_idx--) & 1;
            possible_expr |= (u64)(is_x ? X_VAR : C_VAR) << (3 * shift);
            st[sp++] = is_x ? X_VAR : C_VAR;
        }
        else
        {
            assert(sp >= 2);
            u64 rhs = st[--sp];
            u64 lhs = st[--sp];

            // Ignore x ^ x
            // Ignore X << x or X >> x
            // TODO (x ^ (X ^ (X ^ x)))
            if ((op == XOR && lhs == X_VAR && rhs == X_VAR) ||
                (SHIFT_OP(op) && rhs == X_VAR))
            {
                valid = false; break;
            }

            st[sp++] = 0;
            possible_expr |= (u64)op << (3 * shift);
        }
    }

    if (valid)
    {
        u64 ss = normalize_sequence<u64>(possible_expr);
        //pr3(possible_expr);
        //pr3r(ss);
        //std::cout << normalize_sequence<std::string>(possible_expr) << "\n\n";
        //if (!seen_hashes.count(ss))
        if (!atrie_exists(&head, ss))
        {
            atrie_insert(&head, ss);
            //seen_hashes.insert(ss);
            return possible_expr;
        }
    }

    return 0;
}

template_expr_vec create_var_placements(u64 expr)
{
    template_expr_vec final_templates;
    constexpr i64 bound = (1ull << (k+1))-2;
    // 0 - x, 1 - unnamed Ci
    // skip 000..00, and 11..111, generates  X0 X1 x ^ ^ X2 >>
    for (u64 bits = 1; bits <= bound; ++bits)
        if (u64 res = judge_varX_placement(expr, bits))
            final_templates.push_back(res);
    return final_templates;
}


namespace fs = std::filesystem;
#define BENCHMARK_HDR "bench.h"
const fs::path TESTING_DIR = "testing_bitmixers";

// TODO (X0 ^ ((x << X1) >> X2))
// or    (X2 ^ ((x << X0) >> X1))
// TODO so many of these types of loops jeez
std::pair<std::string, i64> rpn_to_string_inc_C(u64 pattern)
{
    i64 c_var_cnt = 0;
    std::vector<std::string> st;
    st.reserve(k+1);
    for (u64 i = 0; i < 2*k+1; ++i)
    {
        u64 op = pattern >> i * 3 & 7;
        if (IS_OPERAND(op))
            st.emplace_back(op_strs[op]);
        else
        {
            auto get_back = [&st, &c_var_cnt](i64 i) {
                return st[st.size() - i] == op_strs[C_VAR] ?
                            (op_strs[C_VAR] + std::to_string(c_var_cnt++))
                            : std::move(st[st.size() - i]);
            };
            std::string rhs = get_back(1);
            std::string lhs = get_back(2);
            st.pop_back(); st.pop_back();

            st.emplace_back("(" + lhs + " " + op_strs[op] + " " + rhs + ")");
        }
    }
    return { st[0], c_var_cnt };
}

#define START_ZERO 0
#define START_PREV 1
#define START_PREV_P1 2
constexpr u64 start_shift = 18;
constexpr u64 end_shift   = 26;
constexpr u64 start_any   = 0 ;
constexpr u64 end_any     = 18;
constexpr u64 constants[] = { 0xff51afd7ed558ccdull,  0xc4ceb9fe1a85ec53ull,
                              0xbf58476d1ce4e5b9ull,  0x94d049bb133111ebull,
                              0x9e3779b97f4a7c15ull,  0x517cc1b727220a95ull,
                              0x6364136223846793ull,  0x5851f42d4c957f2dull,
                              0x2d358dccaa6c78a5ull,  0x7fb5d329728ea1e5ull,
                              0x9ddfea08eb382d69ull, 0x2127599bf4325c37ull,
                              0xef90881975306691ull, 17316035218449499591ull,
                              0x1c69b3f74ac3ae35ull, 0xf1357aea2e62a9c5ull,
                              0xd1342543de82ef95ull, 0x12e15e35b500f16eull,
                              33, 31, 27, 23, 
                              25, 13, 17, 19};

// limited to A-Z
#define IS_C_VAR(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_ALPHA(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))
void set_loop_constants(u64 expr, u64 *bounds)
{
    std::vector<char> st;
    std::string nstr = normalize_sequence<std::string, true>(expr);

    auto rit = nstr.rbegin();
    while (rit != nstr.rend())
    {
        char op = *rit;
        if (op == '(') {}
        else if (op == ')') st.push_back(')');
        else if (IS_ALPHA(op)) st.push_back(op);
        else // operator
        {
            bool is_shift = op == '>' || op == '<';
            if (is_shift) ++rit;

            auto set_bounds = [bounds](u64 start, u64 end, i64 i) {
                bounds[2*i] = start; bounds[2*i +1] = end;
            };

            // get rid of leading x
            while (st.back() == 'x') st.pop_back();

            // TODO count a better way
            i64 c_var_cnt = 0, idx = st.size() - 1;
            while (st[idx] != ')')
                if (st[idx--] != 'x')
                    c_var_cnt++;

            // if AB, then lhs=A, rhs=B
            if (is_shift) // either xA, AB
            {
                if (c_var_cnt == 2) // has to be <<AB
                {
                    set_bounds(start_any, end_any, st.back() - C_VAR_CH);
                    st.pop_back();
                }
                set_bounds(start_shift, end_shift, st.back() - C_VAR_CH);
                st.pop_back();
            }
            else 
            {
                i64 c_var_cnt = 0;
                while (st.back() != ')')
                {
                    if (!c_var_cnt) set_bounds(start_any, end_any, st.back() - C_VAR_CH);
                    else set_bounds(op=='^' ? START_PREV_P1 : START_PREV, end_any, st.back() - C_VAR_CH);
                    c_var_cnt++;
                    st.pop_back();
                }
            }
            st.pop_back(); // pop rparen
        }
        ++rit;
    }
}

/*
hold test results in separate arrays
sort proxy indices instead of test data
*/
void setup_benchmark();

struct ThreadArgs
{
    const template_expr_vec *exprs;
    int my_id;
};

struct FWriter
{
    FWriter(int sz) 
        : ptr(0), max_sz(sz), buf(static_cast<char *>(malloc(sz))) {}

    void resize()
    {
        max_sz *= 2;
        void *nb = realloc(buf, max_sz);
        assert(nb);
        buf = static_cast<char *>(nb);
    }

    template <typename T>
    void add_one(const T &v)
    {
        using U = std::remove_cv_t<std::remove_reference_t<T>>;
        if constexpr (std::is_same_v<U, char>)
        {
            if (ptr + 1 >= max_sz) resize();
            buf[ptr++] = v;
        }
        else if constexpr (std::is_integral_v<U>)
        {
            u64 pow10 = 1;
            while (pow10 <= v / 10)
                pow10 *= 10;

            T tmp = v;
            while (pow10 > 0)
            {
                T digit = tmp / pow10;
                if (ptr + 1 >= max_sz) resize();
                buf[ptr++] = char('0' + digit);
                tmp %= pow10; pow10 /= 10;
            }
        }
        else if constexpr (std::is_convertible_v<U, const char*>)
        {
            int sz = strlen(v);
            if (ptr + sz >= max_sz) resize();
            std::memcpy(buf + ptr, v, sz);
            ptr += sz;
        }
        else if constexpr (std::is_same_v<U, std::string> ||
                           std::is_same_v<U, std::string_view>)
        {
            if (ptr + v.size() >= max_sz) resize();
            std::memcpy(buf + ptr, v.data(), v.size());
            ptr += v.size();
        }
        else assert(0);
    }

    template <typename ...Args>
    void add(const Args&...args)
    {
        (add_one(args), ...);
    }

    void flush() { assert(write(fd, buf, ptr) == ptr); ptr = 0; }
    void flush_n_close() { assert(write(fd, buf, ptr) == ptr); ptr = 0; close(fd); }

    //void swap_files(

int fd, ptr, max_sz;
char *buf;
};

#define THREAD_WDIR "thread_business"
#define BENCHMARK_FN "run"
class FileState
{
public:
    FileState(const std::string &basename, int sz) 
        : filenum(-1), basename(basename), in_body(sz), out_body(sz) {}

    void load_next_file()
    {
        filenum++;
        const std::string p = basename + std::to_string(filenum) + ".cpp";
        in_body.fd = out_body.fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        assert(in_body.fd);
        out_body.add("#include <array>\n", "#include <cstdint>\n", 
                     "#include \"bench.h\"\n", "using u64 = uint64_t;\n");
        in_body.add("extern \"C\" __attribute__((visibility(\"default\"))) ");
        in_body.add("void ", BENCHMARK_FN, "(BenchResArr &results){\n");
    }

    void flush_file()
    {
        out_body.flush();
        in_body.add("}\n");
        in_body.flush_n_close();
    }

    inline int get_filenum() { return filenum; }

    inline void format_fn_structs(u64 fn_id, u64 fn_num, 
            int arg_cnt, const std::string &fn_body)
    {
        // struct _id_num{u64 
        out_body.add("struct _", fn_id, "_", fn_num, "{u64 ");
        for (int i = 0; i < arg_cnt; ++i) // A0,A1..;
            out_body.add(op_strs[C_VAR], i, (i + 1 < arg_cnt ? ',' : ';'));
        out_body.add("u64 operator()(u64 x)const{return", fn_body, ";}};\n");
    }

    inline void format_arrays(u64 fn_id, int fn_num, int arg_cnt, u64 *bounds)
    {
        // TODO shorten words?
        out_body.add("constexpr std::array<int,", arg_cnt, ">is_assoc_", fn_id, '_', fn_num, "={");
        in_body.add("std::array<int,", 2*arg_cnt, ">bounds_", fn_id, '_', fn_num, "={");
        for (int i = 0; i < arg_cnt; ++i)
        {
            int start = bounds[2*i], end = bounds[2*i+1];
            if (start == START_PREV_P1) { out_body.add('2'); in_body.add("-1"); }
            else if (start == START_PREV) { out_body.add('1'); in_body.add("-1"); }
            else { out_body.add('0'); in_body.add(start); }
            in_body.add(',', end);
            if (i + 1 < arg_cnt) { out_body.add(','); in_body.add(','); }
            else { out_body.add("};\n"); in_body.add("};\n"); }
        }
    }

    inline void format_benchmark_fn_call(u64 fn_id, u64 fn_num, u64 pattern, int arg_cnt)
    {
        in_body.add("gen_nested_loops_and_bench<", arg_cnt, ',', arg_cnt, ',', "is_assoc_", fn_id, "_", fn_num, ',',
                '_', fn_id, '_', fn_num, ">(results,bounds_", fn_id, "_", fn_num, ',', pattern, ',', "0);\n");
    }

private:
    int filenum;
    const std::string &basename;
    FWriter in_body, out_body;
};


std::string join_pattern_and_constants(u64 pattern, u64 indicesp1)
{
    int shift = -5;
    std::vector<std::string> st; st.reserve(k+1);

    for (u64 i = 0; i < 2*k+1; ++i)
    {
        u64 op = pattern >> i * 3 & 7;
        if (IS_OPERAND(op))
            st.emplace_back(op_strs[op]);
        else
        {
            auto get_back = [&st, &shift, indicesp1](i64 i, const u64 *cs) {
                return st[st.size() - i] == op_strs[C_VAR] ?
                            std::to_string(cs[((indicesp1 >> (shift += 5)) & 0x1f) - 1])
                            : std::move(st[st.size() - i]);
            };

            std::string rhs = get_back(1, constants);
            std::string lhs = get_back(2, constants);
            st.pop_back(); st.pop_back();

            st.emplace_back("(" + lhs + " " + op_strs[op] + " " + rhs + ")");
        }
    }
    return st[0];
}

// thread file names: [thread_id]_[file_number].cpp
static_assert(k <= 12, "Bit packing in bench_res struct");
constexpr int num_benchmarks = 2; // NOTE Keep up to def with hdr()
constexpr int num_trials = 100'000;
constexpr int num_threads = 10;
constexpr int max_fns_per_file = 100;
constexpr u64 worst_b1 = -1;
constexpr u64 worst_b2 = -1;
constexpr double worst_b3 = DBL_MAX;
extern "C" char **environ;

// TODO keep up to date with other
struct BenchRes
{
    u64 res;
    u64 indices; // into constants SUBTRACT 1 [1, 26]
    u64 pattern; // A A A x * * * treat as A2 A1 A0 x * * *

    void operator=(const BenchRes &other)
    {
        res = other.res; indices = other.indices; pattern = other.pattern;
    }
};
using BenchResArr = std::array<BenchRes, num_benchmarks>;

typedef void (*dylibfn)(BenchResArr &);
void run(BenchResArr &best_results, const std::string &dylib)
{
    dlerror();
    void *handle = dlopen(dylib.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle) 
    {
        const char* err = dlerror();
        fprintf(stderr, "dlopen failed: %s\n", err ? err : "unknown error");
        abort();
    }

    dlerror();
    dylibfn fn = (dylibfn)dlsym(handle, "run");
    const char *dlsym_error = dlerror();
    if (dlsym_error) 
    {
        std::cout << "dlsym failed: " << dlsym_error << "\n";
        dlclose(handle);
        abort();
    }

    fn(best_results);
    dlclose(handle);
}

void *thread_run_create_compile(void *args)
{
    ThreadArgs *targs = static_cast<ThreadArgs *>(args);
    const template_expr_vec &exprs = *targs->exprs;
    int my_id = targs->my_id, num_functions = 0;

    BenchResArr *best_res = new BenchResArr {{
        {worst_b1,0,0},
        {worst_b2,0,0},
        //{worst_b3,0,0}
    }};

    alignas(8) u64 bounds[2*k]; // NOTE change this to k+1 if you every add 4 free vars
    u64 val = 0x0000000100000000ull;

    const std::string basename = THREAD_WDIR + ("/" + std::to_string(my_id) + "_");

    FileState state(basename, 1 << 13);
    state.load_next_file();

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    // pid2file[pid] = filenum
    u64 cnt = 0, files_compiling = 0;
    std::unordered_map<int, int> pid2file;
    auto compile_file = [&pid2file, &basename](int filenum) {
        const std::string src = basename + std::to_string(filenum) + ".cpp";
        const std::string out = basename + std::to_string(filenum) + ".dylib";
        char *argv[] = {
            (char *)"clang++",
            (char *)"-dynamiclib",
            (char *)"-O3",
            (char *)"-o",
            (char *)out.c_str(),
            (char *)src.c_str(),
            nullptr
        };

        int pid;
        assert(!posix_spawnp(
            &pid,
            "clang++",
            nullptr,
            nullptr,
            argv,
            environ
        ));
        pid2file[pid] = filenum;
    };

    // flag = 0 OR flag = WNOHANG
    auto check_waitpid_and_dispatch = [my_id, &pid2file, best_res, &basename](int flag) {
        if (!pid2file.empty())
        {
            int status, pid;
            while ((pid = waitpid(-1, &status, flag)) > 0)
            {
                const std::string dylib = basename + std::to_string(pid2file[pid]) + ".dylib";
                const std::string file = basename + std::to_string(pid2file[pid]) + ".cpp";

                int fd = open(dylib.c_str(), O_RDONLY);
                if (fd != -1) { fcntl(fd, F_FULLFSYNC); close(fd); }

                run(*best_res, dylib.c_str());
                unlink(dylib.c_str());
                unlink(file.c_str());
            }
        }
    };

    // k=3 : 198, 342431 total iter
    for (u64 i = my_id; i < exprs.size(); i += num_threads)
    {
        if (num_functions >= max_fns_per_file)
        {
            state.flush_file();
            compile_file(state.get_filenum());
            state.load_next_file();
            num_functions = 0;
        }

        u64 fn_id = exprs[i];
        template_expr_vec patterns = create_var_placements(fn_id);
        for (u64 j = 0; j < patterns.size(); ++j)
        {
            u64 pattern = patterns[j];
            std::fill_n(reinterpret_cast<u64 *>(bounds), k, val);
            auto [fn_str, arg_cnt] = rpn_to_string_inc_C(pattern);

            set_loop_constants(pattern, bounds);
            state.format_fn_structs(fn_id, j, arg_cnt, fn_str);
            state.format_arrays(fn_id, j, arg_cnt, bounds);
            state.format_benchmark_fn_call(fn_id, j, pattern, arg_cnt);
        }
        num_functions += patterns.size();
        check_waitpid_and_dispatch(WNOHANG);
    }

    if (num_functions > 0)
    {
        state.flush_file();
        compile_file(state.get_filenum());
    }
    check_waitpid_and_dispatch(0);
    posix_spawn_file_actions_destroy(&actions);

    std::string report = "Thread " + std::to_string(my_id) + " reports\n";
    for (int i = 0; i < num_benchmarks; ++i) 
    {
        BenchRes &best = (*best_res)[i];
        report += " : benchmark " + std::to_string(i) +
                  ": res=" + std::to_string(best.res) + ", ";
        report += join_pattern_and_constants(best.pattern, best.indices) + "\n";
        // Lines 3-5: indices parsing
        int bits = 64 - __builtin_clzll(best.indices | 1);
        for (int shift = 0; shift < bits; shift += 5)
            report += std::to_string((best.indices >> shift) & 0b11111) + " ";
        report += "\n";
    }
    std::cout << report << "\n";

    return best_res; // return best set of BenchArr
}


// TODO turn i64o JIT expressions
// would be cool here, but slower

// watch out: (X*X) ^ (X*X)
// don't allow a ^ a, 
int main()
{
    template_expr_vec exprs = gen_template_expr(k);
    std::sort(exprs.begin(), exprs.end());

    setup_benchmark();

    ThreadArgs args[num_threads];
    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; ++i)
    {
        args[i] = { &exprs, i };
        pthread_create(&threads[i], nullptr, thread_run_create_compile, &args[i]);
    }

    for (int i = 0; i < num_threads; ++i)
    {
        void *ret;
        pthread_join(threads[i], &ret);
        BenchRes *res = (BenchRes *)ret;
        delete res;
    }
}

void setup_benchmark()
{
    if (!mkdir(THREAD_WDIR, 0755))
    {
        struct stat st;
        assert(stat(THREAD_WDIR, &st) == 0 && S_ISDIR(st.st_mode));
    }
    std::string hdr = 
R"cpp(
#pragma once
#include <cstdint>
#include <cstring>
#include <float.h>
#include <functional>
#include <random>
#include <time.h>
#include <tuple>
#include <utility>
#include <vector>

#include "../flat_hash_map.h"

using u64 = uint64_t;
constexpr u64 worst_b1 = -1;
constexpr u64 worst_b2 = -1;
constexpr u64 worst_b3 = -1;
constexpr int k = {{K}};
constexpr int num_benchmarks = {{NB}};
constexpr int num_trials = {{NT}};
constexpr u64 constants[] = { 0xff51afd7ed558ccdull,  0xc4ceb9fe1a85ec53ull,
                              0xbf58476d1ce4e5b9ull,  0x94d049bb133111ebull,
                              0x9e3779b97f4a7c15ull,  0x517cc1b727220a95ull,
                              0x6364136223846793ull,  0x5851f42d4c957f2dull,
                              0x2d358dccaa6c78a5ull,  0x7fb5d329728ea1e5ull,
                              0x9ddfea08eb382d69ull, 0x2127599bf4325c37ull,
                              0xef90881975306691ull, 17316035218449499591ull,
                              0x1c69b3f74ac3ae35ull, 0xf1357aea2e62a9c5ull,
                              0xd1342543de82ef95ull, 0x12e15e35b500f16eull,
                              33, 31, 27, 23,
                              25, 13, 17, 19};

thread_local std::vector<u64> test_data = [] {
    std::vector<u64> tmp(num_trials);
    std::mt19937 rng(0);
    std::uniform_int_distribution<u64> dist(0, UINT64_MAX);
    for (u64 i = 0; i < tmp.size(); ++i)
        tmp[i] = dist(rng);
    return tmp;
}();

struct BenchRes
{
    u64 res;
    u64 indices; // into constants SUBTRACT 1 [1, 26]
    u64 pattern; // A A A x * * * treat as A2 A1 A0 x * * *

    void operator=(const BenchRes &other)
    {
        res = other.res; indices = other.indices; pattern = other.pattern;
    }
};

using BenchResArr = std::array<BenchRes, num_benchmarks>;
using BenchResTuple = std::tuple<BenchRes, BenchRes>;
static_assert(std::tuple_size_v<BenchResTuple> == num_benchmarks,
              "Tuple size does not match num_benchmarks!");

template <size_t I> struct BenchNum {};

// Evaluation speed, lower is better
inline void cmp_benchmark(BenchNum<0>, BenchRes cur, BenchRes &prev)
{
    if (cur.res < prev.res)
        prev = cur;
}

// Bit bias, lower is better
inline void cmp_benchmark(BenchNum<1>, BenchRes cur, BenchRes &prev)
{
    if (cur.res < prev.res)
        prev = cur;
}

// Avalanche Test, closest to 32 is better
inline void cmp_benchmark(BenchNum<2>, BenchRes cur, BenchRes &prev)
{
    constexpr double target = 32.0;
    double dist_cur = abs(cur.res - target);
    double dist_prev = abs(prev.res - target);
    if (dist_cur < dist_prev)
        prev = cur;
}

template <size_t ...Is>
inline void update_benchmarks(const BenchResTuple &cur_result, BenchResArr &prev_results,
             std::index_sequence<Is...>)
{
    (cmp_benchmark(BenchNum<Is>{}, std::get<Is>(cur_result), std::get<Is>(prev_results)), ...);
}

template <typename F>
inline auto benchmark(F &&f, u64 indices, u64 pattern,
                            std::integral_constant<int, 3>)
{
    static_assert(num_benchmarks == 3);
    // SPEED
    volatile u64 sink = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (u64 x : test_data)
        sink ^= f(x);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    double seconds = diff.count();
    double ns_per_call = (seconds * 1e9) / static_cast<double>(test_data.size());

    // BIT BIAS
    constexpr int BITS = 64;
    std::array<u64, BITS> bitcount{};
    for (u64 x : test_data) {
        u64 y = f(x);
        for (int b = 0; b < BITS; ++b)
            bitcount[b] += (y >> b) & 1ULL;
    }
    double max_bias = 0.0;
    for (int b = 0; b < BITS; ++b) {
        double ratio = double(bitcount[b]) / double(num_trials);
        double bias = std::abs(ratio - 0.5);
        if (bias > max_bias)
            max_bias = bias;
    }

    // AVALANCE
    double total_flips = 0.0;
    for (auto x : test_data)
    {
        u64 y = f(x);
        for (int b = 0; b < 64; ++b) {
            u64 x2 = x ^ (1ULL << b);
            u64 y2 = f(x2);
            total_flips += __builtin_popcountll(y ^ y2);
        }
    }
    double avg_flips = total_flips / double(test_data.size() * 64);

    // TODO Variance/low bit test

    BenchRes b1 = { 0, indices, pattern };
    BenchRes b2 = { 0, indices, pattern };
    BenchRes b3 = { 0, indices, pattern };
    return std::tuple{ b1, b2, b3 };
}

template <typename F>
inline auto benchmark(F &&f, u64 indices, u64 pattern,
                                    std::integral_constant<int, 2>)
{
    static_assert(num_benchmarks == 2);
    flat_hash_map<F, u64, u64>  map;

    uint64_t start = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    for (auto it = test_data.begin(); it != test_data.end(); ++it)
        map.insert(*it, (*it) * 2);
    uint64_t end = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    u64 ins_ns = end - start;

    std::shuffle(test_data.begin(), test_data.end(), std::mt19937{42});

    u64 checksum = 0;
    start = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    for (auto it = test_data.rbegin(); it != test_data.rend(); ++it)
    {
        auto res = map.find(*it);
        if (res != map.end()) checksum += res->first + res->second;
    }
    end = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    u64 find_ns = end - start;

    BenchRes b1 = { ins_ns, indices, pattern };
    BenchRes b2 = { find_ns, indices, pattern };
    return std::tuple{ b1, b2 };
}


template <int N, int K,
          const std::array<int, K>& is_assoc,
          typename F, typename ...Args>
inline void gen_nested_loops_and_bench(BenchResArr &results,
                                std::array<int, 2*K> &bounds,
                                u64 pattern, u64 indices, Args ...args)
{
    static_assert(N <= 12, "Adjust bit packing logic");
    if constexpr (N == 0)
    {
        update_benchmarks(
            benchmark(F {constants[args]...}, indices, pattern,
                                std::integral_constant<int, num_benchmarks>{}),
            results,
            std::make_index_sequence<num_benchmarks>{}
        );
    }
    else
    {
        constexpr int level = sizeof...(args);
        constexpr int shift = std::get<level>(is_assoc) == 2 ? 1 : 0; // to prevent X ^ X
        constexpr int prev_idx = 2*(level - 1); // level=0 always is_assoc=false
        constexpr int my_idx = 2*level;
        int assign_idx = std::get<level>(is_assoc) ? prev_idx : my_idx;

        for (bounds[my_idx] = bounds[assign_idx] + shift; bounds[my_idx] < bounds[my_idx+1]; ++bounds[my_idx])
            gen_nested_loops_and_bench<N-1, K, is_assoc, F>(results, bounds, pattern,
                    (bounds[my_idx]+1) << (5 * level) | indices, args..., bounds[my_idx]);
    }
}
)cpp";
    hdr.replace(hdr.find("{{K}}"), 5, std::to_string(k));
    hdr.replace(hdr.find("{{NB}}"), 6, std::to_string(num_benchmarks));
    hdr.replace(hdr.find("{{NT}}"), 6, std::to_string(num_trials));

    const std::string fname = THREAD_WDIR + (std::string("/") + "bench.h");
    int fd = open(fname.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0);
    assert(write(fd, hdr.c_str(), hdr.size()) == hdr.size());
}

