#pragma once

// Case ABI shared by random_parity_corpus.h and random_parity.slang. A case is two
// uint4 words, (kind, key low, key high, bound) then a kind-specific operand;
// the kernel writes one uint4 result per case.
#define LUDUS_RANDOM_PARITY_CASE_PHILOX 0
#define LUDUS_RANDOM_PARITY_CASE_SAMPLE 1
#define LUDUS_RANDOM_PARITY_CASE_MULTIPLY 2
#define LUDUS_RANDOM_PARITY_CASE_CONSTANTS 3
#define LUDUS_RANDOM_PARITY_CASE_FORCED_EXHAUSTION 4
#define LUDUS_RANDOM_PARITY_CASE_FORCED_LAST_ATTEMPT 5
#define LUDUS_RANDOM_PARITY_CASE_FORCED_CONSTANT 6
#define LUDUS_RANDOM_PARITY_CASE_FLOAT 7
#define LUDUS_RANDOM_PARITY_CASE_PADDING 8
#define LUDUS_RANDOM_PARITY_WORDS_PER_CASE 2
#define LUDUS_RANDOM_PARITY_GROUP_SIZE 64
// Result word 3 of a sample case: bit 0 = bounded sample succeeded,
// bit 1 = address within layout v1.
#define LUDUS_RANDOM_PARITY_IS_SAMPLED 1
#define LUDUS_RANDOM_PARITY_IS_IN_RANGE 2
