#ifdef SPOA_TEST_MAIN_

#include <stdio.h>
#include <stdlib.h>

#include "spoa.hpp"
#include "chain.hpp"

int main(int argc, char** argv) {

    ChainSet reads;
    createChainSet(reads, argv[1]);

    std::vector<std::string> sequences;
    for (const auto& it: reads) {
        sequences.emplace_back(it->data());
    }

    std::string consensus = SPOA::generate_consensus(sequences, AlignmentParams(
        atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
        (AlignmentType) atoi(argv[6])), true);

    fprintf(stderr, "Consensus (%zu)\n", consensus.size());
    fprintf(stderr, "%s\n", consensus.c_str());

    return 0;
}

#endif
