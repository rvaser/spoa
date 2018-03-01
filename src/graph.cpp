/*!
 * @file graph.cpp
 *
 * @brief Graph class source file
 */

#include <assert.h>
#include <algorithm>
#include <stack>

#include "spoa/graph.hpp"

namespace spoa {

constexpr uint32_t kMaxAlphabetSize = 256;

std::unique_ptr<Node> Graph::createNode(uint32_t id, uint32_t code) {
    return std::unique_ptr<Node>(new Node(id, code));
}

Node::Node(uint32_t id, uint32_t code)
        : id_(id), code_(code), in_edges_(), out_edges_(),
        aligned_nodes_ids_() {
}

Node::~Node() {
}

uint32_t Node::coverage() const {

    std::unordered_set<uint32_t> label_set;
    for (const auto& edge: in_edges_) {
        for (const auto& label: edge->sequence_labels_) {
            label_set.insert(label);
        }
    }
    for (const auto& edge: out_edges_) {
        for (const auto& label: edge->sequence_labels_) {
            label_set.insert(label);
        }
    }
    return label_set.size();
}

std::unique_ptr<Edge> Graph::createEdge(uint32_t begin_node_id,
    uint32_t end_node_id, uint32_t label, uint32_t weight) {

    return std::unique_ptr<Edge>(new Edge(begin_node_id, end_node_id, label,
        weight));
}

Edge::Edge(uint32_t begin_node_id, uint32_t end_node_id, uint32_t label,
    uint32_t weight)
        : begin_node_id_(begin_node_id), end_node_id_(end_node_id),
        sequence_labels_(1, label), sequence_weights_(1, weight),
        total_weight_(weight) {
}

Edge::~Edge() {
}

void Edge::add_sequence(uint32_t label, uint32_t weight) {
    sequence_labels_.emplace_back(label);
    sequence_weights_.emplace_back(weight);
    total_weight_ += weight;
}

std::unique_ptr<Graph> createGraph() {
    return std::unique_ptr<Graph>(new Graph());
}

Graph::Graph()
        : num_sequences_(0), num_codes_(0), coder_(kMaxAlphabetSize, -1),
        decoder_(kMaxAlphabetSize, -1), nodes_(), rank_to_node_id_(),
        sequences_begin_nodes_ids_(), consensus_() {
}

Graph::~Graph() {
}

uint32_t Graph::add_node(uint32_t code) {
    uint32_t node_id = nodes_.size();
    nodes_.emplace_back(createNode(node_id, code));
    return node_id;
}

void Graph::add_edge(uint32_t begin_node_id, uint32_t end_node_id,
    uint32_t weight) {

    assert(begin_node_id < nodes_.size() && end_node_id < nodes_.size());

    for (const auto& edge: nodes_[begin_node_id]->out_edges_) {
        if (edge->end_node_id_ == end_node_id) {
            edge->add_sequence(num_sequences_, weight);
            return;
        }
    }

    std::shared_ptr<Edge> edge = createEdge(begin_node_id, end_node_id,
        num_sequences_, weight);
    nodes_[begin_node_id]->out_edges_.emplace_back(edge);
    nodes_[end_node_id]->in_edges_.emplace_back(edge);
}

void Graph::add_alignment(const Alignment& alignment,
    const std::string& sequence, uint32_t weight) {

    this->add_alignment(alignment, sequence.c_str(), sequence.size(), weight);
}

void Graph::add_alignment(const Alignment& alignment, const char* sequence,
    uint32_t sequence_size, uint32_t weight) {

    std::vector<uint32_t> weights(sequence_size, weight);
    this->add_alignment(alignment, sequence, sequence_size, weights);
}

void Graph::add_alignment(const Alignment& alignment, const std::string& sequence,
    const std::string& quality) {

    this->add_alignment(alignment, sequence.c_str(), sequence.size(),
        quality.c_str(), quality.size());
}

void Graph::add_alignment(const Alignment& alignment, const char* sequence,
    uint32_t sequence_size, const char* quality, uint32_t quality_size) {

    std::vector<uint32_t> weights;
    for (uint32_t i = 0; i < quality_size; ++i) {
        weights.emplace_back(static_cast<uint32_t>(quality[i] - 33)); // PHRED quality
    }
    this->add_alignment(alignment, sequence, sequence_size, weights);
}

void Graph::add_alignment(const Alignment& alignment, const std::string& sequence,
    const std::vector<uint32_t>& weights) {

    this->add_alignment(alignment, sequence.c_str(), sequence.size(), weights);
}

void Graph::add_alignment(const Alignment& alignment, const char* sequence,
    uint32_t sequence_size, const std::vector<uint32_t>& weights) {

    if (sequence_size == 0) {
        fprintf(stderr, "[spoa::Graph::add_alignment] error: "
            "empty sequence!\n");
        exit(1);
    }
    if (sequence_size != weights.size()) {
        fprintf(stderr, "[spoa::Graph::add_alignment] error: "
            "sequence and weights are of unequal size!");
        exit(1);
    }

    for (uint32_t i = 0; i < sequence_size; ++i) {
        auto c = sequence[i];
        if (coder_[c] == -1) {
            coder_[c] = num_codes_;
            decoder_[num_codes_] = c;
            ++num_codes_;
        }
    }

    if (alignment.empty()) { // no alignment
        int32_t begin_node_id = this->add_sequence(sequence, weights, 0,
            sequence_size);
        ++num_sequences_;
        sequences_begin_nodes_ids_.emplace_back(begin_node_id);

        this->topological_sort();
        return;
    }

    std::vector<uint32_t> valid_seq_ids;
    for (const auto& it: alignment) {
        if (it.second != -1) {
            valid_seq_ids.emplace_back(it.second);
        }
    }

    assert(valid_seq_ids.front() <= sequence_size);
    assert(valid_seq_ids.back() + 1 <= sequence_size);

    uint32_t tmp = nodes_.size();
    int32_t begin_node_id = this->add_sequence(sequence, weights, 0,
        valid_seq_ids.front());
    int32_t head_node_id = tmp == nodes_.size() ? -1 : nodes_.size() - 1;

    int32_t tail_node_id = this->add_sequence(sequence, weights,
        valid_seq_ids.back() + 1, sequence_size);

    int32_t new_node_id = -1;
    float prev_weight = head_node_id == -1 ?
        0 : weights[valid_seq_ids.front() - 1];

    for (uint32_t i = 0; i < alignment.size(); ++i) {
        if (alignment[i].second == -1) {
            continue;
        }

        char letter = sequence[alignment[i].second];
        if (alignment[i].first == -1) {
            new_node_id = this->add_node(coder_[letter]);

        } else {
            if (decoder_[nodes_[alignment[i].first]->code_] == letter) {
                new_node_id = alignment[i].first;

            } else {
                int32_t aligned_to_node_id = -1;
                for (const auto& aid: nodes_[alignment[i].first]->aligned_nodes_ids_) {
                    if (decoder_[nodes_[aid]->code_] == letter) {
                        aligned_to_node_id = aid;
                        break;
                    }
                }

                if (aligned_to_node_id == -1) {
                    new_node_id = this->add_node(coder_[letter]);

                    for (const auto& aid: nodes_[alignment[i].first]->aligned_nodes_ids_) {
                        nodes_[new_node_id]->aligned_nodes_ids_.emplace_back(aid);
                        nodes_[aid]->aligned_nodes_ids_.emplace_back(new_node_id);
                    }

                    nodes_[new_node_id]->aligned_nodes_ids_.emplace_back(
                        alignment[i].first);
                    nodes_[alignment[i].first]->aligned_nodes_ids_.emplace_back(
                        new_node_id);

                } else {
                    new_node_id = aligned_to_node_id;
                }
            }
        }

        if (begin_node_id == -1) {
            begin_node_id = new_node_id;
        }

        if (head_node_id != -1) {
            // both nodes contribute to edge weight
            this->add_edge(head_node_id, new_node_id,
                prev_weight + weights[alignment[i].second]);
        }

        head_node_id = new_node_id;
        prev_weight = weights[alignment[i].second];
    }

    if (tail_node_id != -1) {
        // both nodes contribute to edge weight
        this->add_edge(head_node_id, tail_node_id,
            prev_weight + weights[valid_seq_ids.back() + 1]);
    }

    ++num_sequences_;
    sequences_begin_nodes_ids_.emplace_back(begin_node_id);

    this->topological_sort();
}

int32_t Graph::add_sequence(const char* sequence, const std::vector<uint32_t>& weights,
    uint32_t begin, uint32_t end) {

    if (begin == end) {
        return -1;
    }

    int32_t first_node_id = this->add_node(coder_[sequence[begin]]);

    uint32_t node_id;
    for (uint32_t i = begin + 1; i < end; ++i) {
        node_id = this->add_node(coder_[sequence[i]]);
        // both nodes contribute to edge weight
        this->add_edge(node_id - 1, node_id, weights[i - 1] + weights[i]);
    }

    return first_node_id;
}

void Graph::topological_sort() {

    rank_to_node_id_.clear();

    // 0 - unmarked, 1 - temporarily marked, 2 - permanently marked
    std::vector<uint8_t> node_marks(nodes_.size(), 0);
    std::vector<bool> check_aligned_nodes(nodes_.size(), true);
    std::stack<uint32_t> nodes_to_visit;

    for (uint32_t i = 0; i < nodes_.size(); ++i) {
        if (node_marks[i] != 0) {
            continue;
        }

        nodes_to_visit.push(i);
        while (nodes_to_visit.size() != 0) {
            uint32_t node_id = nodes_to_visit.top();
            bool valid = true;

            if (node_marks[node_id] != 2) {
                for (const auto& edge: nodes_[node_id]->in_edges_) {
                    if (node_marks[edge->begin_node_id_] != 2) {
                        nodes_to_visit.push(edge->begin_node_id_);
                        valid = false;
                    }
                }

                if (check_aligned_nodes[node_id]) {
                    for (const auto& aid: nodes_[node_id]->aligned_nodes_ids_) {
                        if (node_marks[aid] != 2) {
                            nodes_to_visit.push(aid);
                            check_aligned_nodes[aid] = false;
                            valid = false;
                        }
                    }
                }

                assert((valid || node_marks[node_id] != 1) &&
                    "Graph is not a DAG!");

                if (valid) {
                    node_marks[node_id] = 2;
                    if (check_aligned_nodes[node_id]) {
                        rank_to_node_id_.push_back(node_id);
                        for (const auto& aid: nodes_[node_id]->aligned_nodes_ids_) {
                            rank_to_node_id_.emplace_back(aid);
                        }
                    }
                } else {
                    node_marks[node_id] = 1;
                }
            }

            if (valid) {
                nodes_to_visit.pop();
            }
        }
    }

    assert(this->is_topologically_sorted() == true);
}

bool Graph::is_topologically_sorted() const {
    assert(nodes_.size() == rank_to_node_id_.size());

    std::vector<bool> visited_nodes(nodes_.size(), false);
    for (uint32_t node_id: rank_to_node_id_) {
        for (const auto& edge: nodes_[node_id]->in_edges_) {
            if (visited_nodes[edge->begin_node_id_] == false) {
                return false;
            }
        }
        visited_nodes[node_id] = true;
    }

    return true;
}

void Graph::generate_multiple_sequence_alignment(std::vector<std::string>& dst,
    bool include_consensus,
    bool include_depths,
    bool include_errors) {

    std::string consensus_str;
    std::vector<int32_t> depths;
    std::vector<int32_t> errors;

    // assign msa id to each node
    std::vector<uint32_t> msa_node_ids(nodes_.size(), 0);
    uint32_t base_counter = 0;
    for (uint32_t i = 0; i < nodes_.size(); ++i) {
        const uint32_t node_id = rank_to_node_id_[i];

        msa_node_ids[node_id] = base_counter;
        for (uint32_t j = 0; j < nodes_[node_id]->aligned_nodes_ids_.size(); ++j) {
            msa_node_ids[rank_to_node_id_[++i]] = base_counter;
        }
        ++base_counter;
    }

    // get the consensus string if we want to either include the consensus, error, or depths
    // in the returned MSA
    if (include_consensus || include_depths || include_errors) {
        consensus_str.resize(base_counter);
        std::fill_n(consensus_str.begin(), base_counter, '-');
        this->traverse_heaviest_bundle();
        //std::string consensus_str(base_counter, '-');
        for (const auto& node_id: consensus_) {
            consensus_str[msa_node_ids[node_id]] =
                decoder_[nodes_[node_id]->code_];
        }
    }

    // initialize depths and errors if they are to be returned
    if (include_depths) depths.resize(consensus_str.size(), 0);
    if (include_errors) errors.resize(consensus_str.size(), 0);

    // extract sequences from graph and create msa strings (add indels(-) where
    // necessary)
    for (uint32_t i = 0; i < num_sequences_; ++i) {
        std::string alignment_str(base_counter, '-');
        uint32_t curr_node_id = sequences_begin_nodes_ids_[i];

        while (true) {
            const uint32_t msa_i = msa_node_ids[curr_node_id];
            const char base_code = decoder_[nodes_[curr_node_id]->code_];
            alignment_str[msa_i] = base_code;

            if (include_depths || include_errors) {
                const char cons_code = consensus_str[msa_i];
                // NB: do not count deletions (i.e. base == '-') in the depths and errors
                // NB: do not count Ns in the sequence in the depths and errors
                if (cons_code != '-' && base_code != '-' && base_code != 'N') {
                    if (include_depths) depths[msa_i] = depths[msa_i] + 1;
                    if (include_errors && base_code != cons_code) {
                        errors[msa_i] = errors[msa_i] + 1;
                    }
                }
            }

            const uint32_t prev_node_id = curr_node_id;
            for (const auto& edge: nodes_[prev_node_id]->out_edges_) {
                for (const auto& label: edge->sequence_labels_) {
                    if (label == i) {
                        curr_node_id = edge->end_node_id_;
                        break;
                    }
                }
                if (prev_node_id != curr_node_id) {
                    break;
                }
            }

            if (prev_node_id == curr_node_id) {
                break;
            }
        }

        dst.emplace_back(alignment_str);
    }

    if (include_consensus) {
        dst.emplace_back(consensus_str);
    }
    if (include_depths) {
        int i = 0;
        std::string depths_str;
        for (const auto& depth: depths) {
            if (consensus_str[i] != '-') {
                if (i > 0) depths_str += ",";
                depths_str += std::to_string(depth);
            }
            i++;
        }
        dst.emplace_back(depths_str);
    }
    if (include_errors) {
        int i = 0;
        std::string errors_str;
        for (const auto& error: errors) {
            if (consensus_str[i] != '-') {
                if (i > 0) errors_str += ",";
                errors_str += std::to_string(error);
            }
            i++;
        }
        dst.emplace_back(errors_str);
    }
}

std::string Graph::generate_consensus() {

    this->traverse_heaviest_bundle();
    std::string consensus_str = "";
    for (const auto& node_id: consensus_) {
        consensus_str += decoder_[nodes_[node_id]->code_];
    }

    return consensus_str;
}

std::string Graph::generate_consensus(std::vector<uint32_t>& dst, bool verbose) {

    auto consensus_str = this->generate_consensus();

    dst.clear();
    if (verbose == false) {
        for (const auto& node_id: consensus_) {
            uint32_t total_coverage = nodes_[node_id]->coverage();
            for (const auto& aid: nodes_[node_id]->aligned_nodes_ids_) {
                total_coverage += nodes_[aid]->coverage();
            }
            dst.emplace_back(total_coverage);
        }
    } else {
        dst.resize((num_codes_ + 1) * consensus_.size(), 0);

        std::vector<uint32_t> msa_node_ids(nodes_.size(), 0);
        uint32_t base_counter = 0;
        for (uint32_t i = 0; i < nodes_.size(); ++i) {
            uint32_t node_id = rank_to_node_id_[i];

            msa_node_ids[node_id] = base_counter;
            for (uint32_t j = 0; j < nodes_[node_id]->aligned_nodes_ids_.size(); ++j) {
                msa_node_ids[rank_to_node_id_[++i]] = base_counter;
            }
            ++base_counter;
        }

        std::vector<uint32_t> consensus_msa_ids;
        for (const auto& node_id: consensus_) {
            consensus_msa_ids.emplace_back(msa_node_ids[node_id]);
        }

        for (uint32_t i = 0; i < num_sequences_; ++i) {
            auto curr_node_id = sequences_begin_nodes_ids_[i];

            uint32_t c = 0;
            while (true) {
                auto msa_id = msa_node_ids[curr_node_id];

                for (; c < consensus_.size() && consensus_msa_ids[c] < msa_id; ++c) {
                    ++dst[num_codes_ * consensus_.size() + c];
                }
                if (c >= consensus_.size()) {
                    continue;
                }
                if (consensus_msa_ids[c] == msa_id) {
                    ++dst[nodes_[curr_node_id]->code_ * consensus_.size() + c];
                    ++c;
                }

                uint32_t prev_node_id = curr_node_id;
                for (const auto& edge: nodes_[prev_node_id]->out_edges_) {
                    for (const auto& label: edge->sequence_labels_) {
                        if (label == i) {
                            curr_node_id = edge->end_node_id_;
                            break;
                        }
                    }
                    if (prev_node_id != curr_node_id) {
                        break;
                    }
                }
                if (prev_node_id == curr_node_id) {
                    break;
                }
            }
        }
    }

    return consensus_str;
}

void Graph::traverse_heaviest_bundle() {

    std::vector<int32_t> predecessors(nodes_.size(), -1);
    std::vector<int32_t> scores(nodes_.size(), -1);

    uint32_t max_score_id = 0;
    for (const auto& node_id: rank_to_node_id_) {
        for (const auto& edge: nodes_[node_id]->in_edges_) {
            if (scores[node_id] < edge->total_weight_ ||
                (scores[node_id] == edge->total_weight_ &&
                scores[predecessors[node_id]] <= scores[edge->begin_node_id_])) {

                scores[node_id] = edge->total_weight_;
                predecessors[node_id] = edge->begin_node_id_;
            }
        }

        if (predecessors[node_id] != -1) {
            scores[node_id] += scores[predecessors[node_id]];
        }

        if (scores[max_score_id] < scores[node_id]) {
            max_score_id = node_id;
        }
    }

    if (nodes_[max_score_id]->out_edges_.size() != 0) {

        std::vector<uint32_t> node_id_to_rank(nodes_.size(), 0);
        for (uint32_t i = 0; i < nodes_.size(); ++i) {
            node_id_to_rank[rank_to_node_id_[i]] = i;
        }

        while (nodes_[max_score_id]->out_edges_.size() != 0) {
            max_score_id = this->branch_completion(scores, predecessors,
                node_id_to_rank[max_score_id]);
        }
    }

    // traceback
    consensus_.clear();
    while (predecessors[max_score_id] != -1) {
        consensus_.emplace_back(max_score_id);
        max_score_id = predecessors[max_score_id];
    }
    consensus_.emplace_back(max_score_id);

    std::reverse(consensus_.begin(), consensus_.end());
}

uint32_t Graph::branch_completion(std::vector<int32_t>& scores,
    std::vector<int32_t>& predecessors, uint32_t rank) {

    uint32_t node_id = rank_to_node_id_[rank];
    for (const auto& edge: nodes_[node_id]->out_edges_) {
        for (const auto& o_edge: nodes_[edge->end_node_id_]->in_edges_) {
            if (o_edge->begin_node_id_ != node_id) {
                scores[o_edge->begin_node_id_] = -1;
            }
        }
    }

    float max_score = 0;
    uint32_t max_score_id = 0;
    for (uint32_t i = rank + 1; i < rank_to_node_id_.size(); ++i) {

        uint32_t node_id = rank_to_node_id_[i];
        scores[node_id] = -1;
        predecessors[node_id] = -1;

        for (const auto& edge: nodes_[node_id]->in_edges_) {
            if (scores[edge->begin_node_id_] == -1) {
                continue;
            }

            if (scores[node_id] < edge->total_weight_ ||
                (scores[node_id] == edge->total_weight_ &&
                scores[predecessors[node_id]] <= scores[edge->begin_node_id_])) {

                scores[node_id] = edge->total_weight_;
                predecessors[node_id] = edge->begin_node_id_;
            }
        }

        if (predecessors[node_id] != -1) {
            scores[node_id] += scores[predecessors[node_id]];
        }

        if (max_score < scores[node_id]) {
            max_score = scores[node_id];
            max_score_id = node_id;
        }
    }

    return max_score_id;
}

// backtracing from right to left!
void Graph::extract_subgraph_nodes(std::vector<bool>& dst,
    uint32_t begin_node_id, uint32_t end_node_id) const {

    dst.resize(nodes_.size(), false);

    std::stack<uint32_t> nodes_to_visit;
    nodes_to_visit.push(begin_node_id);

    while (nodes_to_visit.size() != 0) {
        uint32_t node_id = nodes_to_visit.top();
        nodes_to_visit.pop();

        if (dst[node_id] == false && node_id >= end_node_id) {
            for (const auto& edge: nodes_[node_id]->in_edges_) {
                nodes_to_visit.push(edge->begin_node_id_);
            }
            for (const auto& aid: nodes_[node_id]->aligned_nodes_ids_) {
                nodes_to_visit.push(aid);
            }

            dst[node_id] = true;
        }
    }
}

std::unique_ptr<Graph> Graph::subgraph(uint32_t begin_node_id,
    uint32_t end_node_id, std::vector<int32_t>& subgraph_to_graph_mapping) const {

    std::vector<bool> is_subgraph_node;
    extract_subgraph_nodes(is_subgraph_node, end_node_id, begin_node_id);

    // init subgraph
    auto subgraph = std::unique_ptr<Graph>(new Graph());
    subgraph->num_sequences_ = num_sequences_;
    subgraph->num_codes_ = num_codes_;
    subgraph->coder_ = std::vector<int32_t>(coder_);
    subgraph->decoder_ = std::vector<int32_t>(decoder_);

    // create mapping from subgraph to graph and vice versa and add nodes to
    // subgraph
    subgraph_to_graph_mapping.resize(nodes_.size(), -1);
    std::vector<int32_t> graph_to_subgraph_mapping(nodes_.size(), -1);

    for (uint32_t i = 0; i < is_subgraph_node.size(); ++i) {
        if (is_subgraph_node[i] == false) {
            continue;
        }

        uint32_t subgraph_id = subgraph->add_node(nodes_[i]->code_);
        graph_to_subgraph_mapping[i] = subgraph_id;
        subgraph_to_graph_mapping[subgraph_id] = i;
    }

    // add edges and aligned nodes
    for (uint32_t i = 0; i < is_subgraph_node.size(); ++i) {
        if (is_subgraph_node[i] == false) {
            continue;
        }

        uint32_t subgraph_id = graph_to_subgraph_mapping[i];

        for (const auto& edge: nodes_[i]->in_edges_) {
            if (graph_to_subgraph_mapping[edge->begin_node_id_] == -1) {
                continue;
            }
            subgraph->add_edge(graph_to_subgraph_mapping[edge->begin_node_id_],
                subgraph_id, edge->total_weight_);
        }
        for (const auto& aid: nodes_[i]->aligned_nodes_ids_) {
            if (graph_to_subgraph_mapping[aid] == -1) {
                continue;
            }
            subgraph->nodes_[subgraph_id]->aligned_nodes_ids_.emplace_back(
                graph_to_subgraph_mapping[aid]);
        }
    }

    subgraph->topological_sort();

    return subgraph;
}

void Graph::update_alignment(Alignment& alignment,
    const std::vector<int32_t>& subgraph_to_graph_mapping) const {

    for (uint32_t i = 0; i < alignment.size(); ++i) {
        if (alignment[i].first != -1) {
            alignment[i].first = subgraph_to_graph_mapping[alignment[i].first];
        }
    }
}

void Graph::print_csv() const {

    std::vector<int32_t> in_consensus(nodes_.size(), -1);
    int32_t rank = 0;
    for (const auto& id: consensus_) {
        in_consensus[id] = rank++;
    }

    printf("digraph %d {\n", num_sequences_);
    printf("    graph [rankdir=LR]\n");
    for (uint32_t i = 0; i < nodes_.size(); ++i) {
        printf("    %d [label = \"%d - %c\"", i, i, decoder_[nodes_[i]->code_]);
        if (in_consensus[i] != -1) {
            printf(", style=filled, fillcolor=goldenrod1");
        }
        printf("]\n");

        for (const auto& edge: nodes_[i]->out_edges_) {
            printf("    %d -> %d [label = \"%d\"", i, edge->end_node_id_,
                edge->total_weight_);
            if (in_consensus[i] + 1 == in_consensus[edge->end_node_id_]) {
                printf(", color=goldenrod1");
            }
            printf("]\n");
        }
        for (const auto& aid: nodes_[i]->aligned_nodes_ids_) {
            if (aid > i) {
                printf("    %d -> %d [style = dotted, arrowhead = none]\n",
                    i, aid);
            }
        }
    }
    printf("}\n");
}

}
