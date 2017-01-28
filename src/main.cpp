#ifdef SPOA_MAIN_

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>

#include "spoa.hpp"
#include "chain.hpp"

#include "bioparser/src/bioparser.hpp"

using namespace spoa;

static struct option options[] = {
    {"fasta-sequences", required_argument, 0, 'a'},
    {"fastq-sequences", required_argument, 0, 'q'},
    {"match", required_argument, 0, 'm'},
    {"mismatch", required_argument, 0, 'x'},
    {"gap-open", required_argument, 0, 'o'},
    {"gap-extend", required_argument, 0, 'e'},
    {"algorithm", required_argument, 0, 'l'},
    {"result", required_argument, 0, 'r'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
};

void help();

int main(int argc, char** argv) {

    std::string fasta_path;
    std::string fastq_path;

    int32_t match = 5;
    int32_t mismatch = -4;
    int32_t gap_open = -8;
    int32_t gap_extend = -6;

    int32_t algorithm = 0;
    int32_t result = 0;

    while (true) {
        auto argument = getopt_long(argc, argv, "a:q:m:x:o:e:l:r:h", options, nullptr);
        if (argument == -1) {
            break;
        }

        switch (argument) {
            case 'a':
                fasta_path = optarg;
                break;
            case 'q':
                fastq_path = optarg;
                break;
            case 'm':
                match = atoi(optarg);
                break;
            case 'x':
                mismatch = atoi(optarg);
                break;
            case 'o':
                gap_open = atoi(optarg);
                break;
            case 'e':
                gap_extend = atoi(optarg);
                break;
            case 'l':
                algorithm = atoi(optarg);
                break;
            case 'r':
                result = atoi(optarg);
                break;
            case 'h':
            default:
                help();
                return -1;
        }
    }

    if (fasta_path.empty() && fastq_path.empty()) {
        printf ("missing option -a/-q (sequences file)\n");
        help ();
        return -1;
    }
    auto params = AlignmentParams(match, mismatch, gap_open, gap_extend, (AlignmentType) algorithm);

    std::vector<std::unique_ptr<Chain>> chains;
    std::vector<std::string> sequences;
    std::vector<std::string> qualities;

    std::shared_ptr<Graph> graph;

    if (!fasta_path.empty()) {
        auto creader = bioparser::createReader<Chain, bioparser::FastaReader>(fasta_path);
        creader->read_objects(chains, -1);

        for (const auto& it: chains) {
            sequences.emplace_back(it->data());
        }

        graph = construct_partial_order_graph(sequences, params, false);

    } else {
        auto creader = bioparser::createReader<Chain, bioparser::FastqReader>(fastq_path);
        creader->read_objects(chains, -1);

        for (const auto& it: chains) {
            sequences.emplace_back(it->data());
            qualities.emplace_back(it->quality());
        }

        graph = construct_partial_order_graph(sequences, qualities, params, false);
    }

    if (result == 0 || result == 2) {
        std::string consensus = graph->generate_consensus();
        fprintf(stderr, "Consensus (%zu)\n", consensus.size());
        fprintf(stderr, "%s\n", consensus.c_str());
    }

    if (result == 1 || result == 2) {
        std::vector<std::string> msa;
        graph->generate_msa(msa);
        fprintf(stderr, "Multiple sequence alignment\n");
        for (const auto& it: msa) {
            fprintf(stderr, "%s\n", it.c_str());
        }
    }

    return 0;
}

void help() {
    printf(
    "usage: spoa -a <FASTA file> (or -q <FASTQ file>) [arguments ...]\n"
    "\n"
    "arguments:\n"
    "    -a, --fasta-sequences <file>\n"
    "        (required)\n"
    "        input FASTA file\n"
    "    -q, --fastq-sequences <file>\n"
    "        (required/alternative of -a)\n"
    "        input FASTQ file\n"
    "    -m, --match <int>\n"
    "        default: 5\n"
    "        score for matching bases (must be positive integer)\n"
    "    -x, --mismatch <int>\n"
    "        default: -4\n"
    "        score for mismatching bases (must be negative integer)\n"
    "    -o, --gap-open <int>\n"
    "        default: -8\n"
    "        gap opening penalty (must be negative integer)\n"
    "    -e, --gap-extend <int>\n"
    "        default: -6\n"
    "        gap extension penalty (must be negative integer)\n"
    "    -l, --algorithm <int>\n"
    "        default: 0\n"
    "        alignment mode: 0 - local, 1 - global, 2 - semi-global\n"
    "    -r, --result <int>\n"
    "        default: 0\n"
    "        result mode: 0 - consensus, 1 - multiple sequence alignment, 2 - 0 + 1\n"
    "    -h, -help\n"
    "        prints out the help\n");
}

#endif
