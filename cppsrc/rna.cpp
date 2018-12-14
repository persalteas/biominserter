#include "rna.h"
#include "rna1995.h"
#include <cassert>
#include <iostream>
#include <omp.h>
#include <string>
#include <vector>

using namespace Eigen;
using std::abs;
using std::cerr;
using std::cout;
using std::endl;

uint min(uint a, uint b) { return a < b ? a : b; }
uint max(uint a, uint b) { return a > b ? a : b; }

RNA::RNA(string name, string seq)
: pair_map(Matrix<pair_t, 5, 5>::Constant(PAIR_OTHER)), name_(name), seq_(seq), n_(seq.size())
{
    bseq_.reserve(n_);
    vector<char> unknown_chars;
    bool         contains_T = false;
    for (char c : seq) {
        if (c == 'T' or c == 't') {
            c          = 'U';
            contains_T = true;
        }
        if (base_type(c) == BASE_N) {
            unknown_chars.push_back(c);
        }
        bseq_.push_back(base_type(c));
    }
    if (contains_T) cout << "\tWARNING: Thymines automatically replaced by uraciles.";
    if (unknown_chars.size() > 0) {
        cout << "\tWARNING: Unknown chars in input sequence ignored : ";
        for (char c : unknown_chars) cout << c << " ";
        cout << endl;
    }
    pair_map(BASE_A, BASE_U) = PAIR_AU;
    pair_map(BASE_U, BASE_A) = PAIR_UA;
    pair_map(BASE_C, BASE_G) = PAIR_CG;
    pair_map(BASE_G, BASE_C) = PAIR_GC;
    pair_map(BASE_G, BASE_U) = PAIR_GU;
    pair_map(BASE_U, BASE_G) = PAIR_UG;
    cout << "\t>sequence formatted" << endl;

    // define pij_
    nrjp_.salt_correction = 0.0;
    nrjp_.loop_greater30  = 1.079;    // 1.75 * RT
    nrjp_.hairpin_GGG     = 0.0;
    // load_parameters("rna1999.dG"); // to load custom parameters
    load_default_parameters();
    cout << "\t>computing pairing probabilities..." << endl;
    compute_basepair_probabilities(false, true);
    cout << "\t\t>pairing probabilities defined" << endl;
}

void RNA::print_basepair_p_matrix(float theta) const
{
    cout << endl << endl;
    cout << "\t=== -log10(p(i,j)) for each pair (i,j) of nucleotides: ===" << endl << endl;
    cout << "\t" << seq_ << endl;
    uint i = 0;
    for (uint u = 0; u < pij_.rows(); u++) {
        cout << "\t";
        for (uint v = 0; v < pij_.cols(); v++) {
            if (pij_(u, v) < 5e-10)
                cout << " ";
            else if (pij_(u, v) > theta)
                cout << "\033[0;32m" << int(-log10(pij_(u, v))) << "\033[0m";
            else
                cout << int(-log10(pij_(u, v)));
        }
        cout << seq_[i] << endl;
        i++;
    }
    cout << endl << "\t\033[0;32mgreen\033[0m basepairs are kept as decision variables." << endl << endl;
}

void RNA::load_default_parameters()
{
    int p[] = {PAIR_AU, PAIR_CG, PAIR_GC, PAIR_UA, PAIR_GU, PAIR_UG};
    int b[] = {BASE_A - 1, BASE_C - 1, BASE_G - 1, BASE_U - 1};

    const int* v = &thermo_params[0];

    // stack
    for (int i = 0; i != 6; ++i)
        for (int j = 0; j != 6; ++j) nrjp_.stack37[p[i]][p[j]] = *(v++) / 100.0;

    // hairpin
    for (int i = 0; i < 30; ++i) nrjp_.hairpin37[i] = *(v++) / 100.0;

    // bulge
    for (int i = 0; i < 30; ++i) nrjp_.bulge37[i] = *(v++) / 100.0;

    // interior
    for (int i = 0; i < 30; ++i) nrjp_.interior37[i] = *(v++) / 100.0;

    // asymmetry panelties
    for (int i = 0; i < 4; ++i) nrjp_.asymmetry_penalty[i] = *(v++) / 100.0;

    // mismatch hairpin
    for (int i = 0; i != 4; ++i)
        for (int j = 0; j != 4; ++j)
            for (int k = 0; k != 6; ++k) nrjp_.mismatch_hairpin37[b[i]][b[j]][p[k]] = *(v++) / 100.0;

    // mismatch interior
    for (int i = 0; i != 4; ++i)
        for (int j = 0; j != 4; ++j)
            for (int k = 0; k != 6; ++k) nrjp_.mismatch_interior37[b[i]][b[j]][p[k]] = *(v++) / 100.0;

    // dangle5
    for (int i = 0; i != 6; ++i)
        for (int j = 0; j != 4; ++j) nrjp_.dangle5_37[p[i]][b[j]] = *(v++) / 100.0;

    // dangle3
    for (int i = 0; i != 6; ++i)
        for (int j = 0; j != 4; ++j) nrjp_.dangle3_37[p[i]][b[j]] = *(v++) / 100.0;

    // multiloop penalties
    nrjp_.a1 = *(v++) / 100.0;
    nrjp_.a2 = *(v++) / 100.0;
    nrjp_.a3 = *(v++) / 100.0;

    // AT terminate penalties
    nrjp_.at_penalty = *(v++) / 100.0;

    // interior loops 1x1
    for (int i = 0; i != 6; ++i)
        for (int j = 0; j != 6; ++j)
            for (int k = 0; k != 4; ++k)
                for (int l = 0; l != 4; ++l) nrjp_.int11_37[p[i]][p[j]][b[k]][b[l]] = *(v++) / 100.0;

    // interior loops 2x2
    for (int i = 0; i != 6; ++i)
        for (int j = 0; j != 6; ++j)
            for (int m = 0; m != 4; ++m)
                for (int n = 0; n != 4; ++n)
                    for (int k = 0; k != 4; ++k)
                        for (int l = 0; l != 4; ++l)
                            nrjp_.int22_37[p[i]][p[j]][b[m]][b[l]][b[n]][b[k]] = *(v++) / 100.0;

    // interior loops 1x2
    for (int i = 0; i != 6; ++i)
        for (int j = 0; j != 6; ++j)
            for (int m = 0; m != 4; ++m)
                for (int k = 0; k != 4; ++k)
                    for (int l = 0; l != 4; ++l) nrjp_.int21_37[p[i]][b[k]][b[m]][p[j]][b[l]] = *(v++) / 100.0;

    // nrjp_.polyC hairpin parameters
    nrjp_.polyC_penalty = *(v++) / 100.0;
    nrjp_.polyC_slope   = *(v++) / 100.0;
    nrjp_.polyC_int     = *(v++) / 100.0;

    // pseudoknot energy parameters
    nrjp_.pk_penalty                    = *(v++) / 100.0;
    nrjp_.pk_paired_penalty             = *(v++) / 100.0;
    nrjp_.pk_unpaired_penalty           = *(v++) / 100.0;
    nrjp_.pk_multiloop_penalty          = *(v++) / 100.0;
    nrjp_.pk_pk_penalty                 = *(v++) / 100.0;
    nrjp_.pk_band_penalty               = 0.0;
    nrjp_.pk_stack_span                 = 1.0;
    nrjp_.pk_interior_span              = 1.0;
    nrjp_.multiloop_penalty_pk          = nrjp_.a1;
    nrjp_.multiloop_paired_penalty_pk   = nrjp_.a2;
    nrjp_.multiloop_unpaired_penalty_pk = nrjp_.a3;

    // BIMOLECULAR TERM
    nrjp_.intermolecular_initiation = *(v++) / 100.0;

    // triloops
    // std::fill(nrjp_.triloop37.data(),
    // nrjp_.triloop37.data()+nrjp_.triloop37.num_elements(), 0.0);
    std::fill(&nrjp_.triloop37[0][0][0][0][0], &nrjp_.triloop37[0][0][0][0][0] + 4 * 4 * 4 * 4 * 4, 0.0);
    for (int i = 0; triloops[i].s != nullptr; ++i) {
        int         v    = triloops[i].e;
        const char* loop = triloops[i].s;
        vector<int> idx(5);
        for (int i = 0; i != 5; ++i) idx[i] = base_type(loop[i]) - 1;
        // nrjp_.triloop37(idx) = v/100.0;
        nrjp_.triloop37[idx[0]][idx[1]][idx[2]][idx[3]][idx[4]] = v / 100.0;
    }

    // tloops
    // std::fill(nrjp_.tloop37.data(),
    // nrjp_.tloop37.data()+nrjp_.tloop37.num_elements(), 0.0);
    std::fill(&nrjp_.tloop37[0][0][0][0][0][0], &nrjp_.tloop37[0][0][0][0][0][0] + 4 * 4 * 4 * 4 * 4 * 4, 0.0);
    for (int i = 0; tetraloops[i].s != nullptr; ++i) {
        int         v    = tetraloops[i].e;
        const char* loop = tetraloops[i].s;
        vector<int> idx(6);
        for (int i = 0; i != 6; ++i) idx[i] = base_type(loop[i]) - 1;
        // nrjp_.tloop37(idx) = v/100.0;
        nrjp_.tloop37[idx[0]][idx[1]][idx[2]][idx[3]][idx[4]][idx[5]] = v / 100.0;
    }
    cout << "\t>default parameters loaded (Serra and Turner, 1995)" << endl;
}

float RNA::GIL_mismatch(uint i, uint j, uint k, uint l) const
{
    return nrjp_.mismatch_interior37[bseq_[k] - 1][bseq_[l] - 1][pair_type(i, j)];
}

float RNA::GIL_mismatch(uint i, uint j) const { return nrjp_.mismatch_interior37[BASE_N][BASE_N][pair_type(i, j)]; }



float RNA::Gpenalty(uint i, uint j) const
{
    return pair_type(i, j) == PAIR_AU || pair_type(i, j) == PAIR_UA ? nrjp_.at_penalty : 0;
}

float_t RNA::GIL_asymmetry(uint l1, uint l2) const
{
    return Gloop(l1 + l2) +
           std::min(nrjp_.max_asymmetry, std::abs((int)l1 - (int)l2) * nrjp_.asymmetry_penalty[std::min((uint)4, std::min(l1, l2)) - 1]);
}

base_t RNA::base_type(char x) const
{
    if (x == 'a' or x == 'A') return BASE_A;
    if (x == 'c' or x == 'C') return BASE_C;
    if (x == 'g' or x == 'G') return BASE_G;
    if (x == 'u' or x == 'U') return BASE_U;
    return BASE_N;
}

pair_t RNA::pair_type(int i) const
{
    // assume Watson-Crick pairs
    switch (bseq_[i]) {
    case BASE_A: return PAIR_AU; break;
    case BASE_C: return PAIR_CG; break;
    case BASE_G: return PAIR_GC; break;
    case BASE_U: return PAIR_UA; break;
    case BASE_N: return PAIR_OTHER;
    }
    return PAIR_OTHER;
}

float RNA::GHL(uint i, uint j) const
{
    float e     = 0.0;
    bool  polyC = true;
    for (uint k = i + 1; k < j; ++k) {
        if (seq_[k] != BASE_C) {
            polyC = false;
            break;
        }
    }

    int size = j - i - 1;

    assert(size >= 3);
    assert(allowed_basepair(i, j));

    e += size <= 30 ? nrjp_.hairpin37[size - 1] : nrjp_.hairpin37[30 - 1] + nrjp_.loop_greater30 * log(size / 30.0);

    if (size == 3) {
        e += Gpenalty(i, j);
        e += nrjp_.triloop37[bseq_[i] - 1][bseq_[i + 1] - 1][bseq_[i + 2] - 1][bseq_[j - 1] - 1][bseq_[j] - 1];
        if (polyC) e += nrjp_.polyC_penalty;
        if (bseq_[i + 1] == BASE_G and bseq_[i + 2] == BASE_G and bseq_[j - 1] == BASE_G) e += nrjp_.hairpin_GGG;
    } else if (size == 4) {
        e += nrjp_.tloop37[bseq_[i] - 1][bseq_[i + 1] - 1][bseq_[i + 2] - 1][bseq_[j - 2] - 1][bseq_[j - 1] - 1][bseq_[j] - 1];
        e += nrjp_.mismatch_hairpin37[bseq_[i + 1] - 1][bseq_[j - 1] - 1][pair_type(i, j)];
        if (polyC) e += nrjp_.polyC_slope * size + nrjp_.polyC_int;
    } else /*if (size>4)*/
    {
        e += nrjp_.mismatch_hairpin37[bseq_[i + 1] - 1][bseq_[j - 1] - 1][pair_type(i, j)];
        if (polyC) e += nrjp_.polyC_slope * size + nrjp_.polyC_int;
    }
    return e;
}

float RNA::GIL(uint i, uint h, uint m, uint j, bool pk) const
{
    int   l1   = h - i - 1;
    int   l2   = j - m - 1;
    int   size = l1 + l2;
    float e    = 0;

    // helix
    if (size == 0) {
        return nrjp_.stack37[pair_type(i, j)][pair_type(h, m)] * (pk ? nrjp_.pk_stack_span : 1.0);
    }

    // bulge
    else if (l1 == 0 || l2 == 0) {
        e += size <= 30 ? nrjp_.bulge37[size - 1] : nrjp_.bulge37[30 - 1] + nrjp_.loop_greater30 * log(size / 30.0);

        if (l1 + l2 == 1)    // single bulge...treat as a stacked region
        {
            e += nrjp_.stack37[pair_type(i, j)][pair_type(h, m)];
            e -= nrjp_.salt_correction;
        } else {
            e += Gpenalty(i, j);
            e += Gpenalty(h, m);
        }
    }

    // interior loop
    else if (l1 > 0 and l2 > 0) {
        int asymmetry = abs(l1 - l2);
        if (asymmetry > 1 || size > 4) {
            e += GIL_asymmetry(l1, l2);
            if (l1 > 1 and l2 > 1) {
                e += GIL_mismatch(m, h, m + 1, h - 1);
                e += GIL_mismatch(i, j, i + 1, j - 1);
            } else if (l1 == 1 || l2 == 1) {
                e += GIL_mismatch(m, h);
                e += GIL_mismatch(i, j);
            } else {
                assert(!"unclassified interior loop");
                exit(1);
            }
        } else if (l1 == 1 and l2 == 1)
            e += nrjp_.int11_37[pair_type(i, j)][pair_type(h, m)][bseq_[i + 1] - 1][bseq_[j - 1] - 1];
        else if (l1 == 2 and l2 == 2)
            e +=
            nrjp_.int22_37[pair_type(i, j)][pair_type(h, m)][bseq_[i + 1] - 1][bseq_[j - 1] - 1][bseq_[i + 2] - 1][bseq_[j - 2] - 1];
        else if (l1 == 1 and l2 == 2)
            e += nrjp_.int21_37[pair_type(i, j)][bseq_[j - 2] - 1][bseq_[i + 1] - 1][pair_type(h, m)][bseq_[j - 1] - 1];
        else if (l1 == 2 and l2 == 1)
            e += nrjp_.int21_37[pair_type(m, h)][bseq_[i + 1] - 1][bseq_[j - 1] - 1][pair_type(j, i)][bseq_[i + 2] - 1];
        else {
            assert(!"error in tabulated interior loop");
            exit(EXIT_FAILURE);
        }
    } else {
        assert(!"improperly classifed interior loop");
        exit(EXIT_FAILURE);
    }
    return e * (pk ? nrjp_.pk_interior_span : 1.0);
}

float RNA::Gloop(uint l) const
{
    return l <= 30 ? nrjp_.interior37[l - 1] : nrjp_.interior37[30 - 1] + nrjp_.loop_greater30 * log(l / 30.0);
}

bool RNA::allowed_basepair(size_t u, size_t v) const
{
    size_t a, b;
    a = (v > u) ? u : v;
    b = (v > u) ? v : u;
    if (b - a < 4) return false;
    if (a >= n_ - 4) return false;
    if (b >= n_) return false;
    return true;
}

vector<MatrixXf> RNA::compute_partition_function_noPK_ON4(void)
{
    // This is the o(N⁴) algorithm from Dirks & Pierce, 2003
    // Basically is the same than McCaskill, 1990
    // Only computes Q, Qb and Qm
    // GML is approximated by a1 + k*a2 + u*a3 (a,b,c in McCaskill)
    // Pseudoknots supposed impossible in the structure

    float RT = kB * AVOGADRO * (ZERO_C_IN_KELVIN + 37.0);
    float a1 = nrjp_.a1;
    float a2 = nrjp_.a2;
    float a3 = nrjp_.a3;

    // O(3N²) space
    MatrixXf Q  = MatrixXf::Zero(n_, n_);
    MatrixXf Qb = MatrixXf::Zero(n_, n_);
    MatrixXf Qm = MatrixXf::Zero(n_, n_);
    int      i, j, d, e, l;
    // case where l=2
    for (i = 0; i < int(n_) - 1; i++) Q(i, i + 1) = 1.0;    // exp(- Gempty / RT) = exp(0) = 1.0
    // cases where l=3, l=4, no hairpins possible
    for (l = 3; l < 5; l++)
        for (i = 0; i <= int(n_) - l; i++) Q(i, i + l - 1) = 1.0;    // exp(-Gempty/RT) (no basepairs between i and j)
    // Cases considering subsequences of growing sizes from 5 until n_
    for (l = 5; l <= int(n_); l++) {
#pragma omp parallel for private(j, d, e)
        for (i = 0; i <= int(n_) - l; i++) {
            j = i + l - 1;    // Consider the subsequence [i,j] of length l

            // Qb recursion
            Qb(i, j) = exp(-GHL(i, j) / RT);
            if (l >= 7) {                           // if there is enough space for an hairpin inside
                for (d = i + 1; d <= j - 5; d++)    // loop over all possible rightmost basepairs (d,e)
                    for (e = d + 4; e <= j - 1; e++) {
                        Qb(i, j) += Qb(d, e) * exp(-GIL(i, d, e, j, false) / RT);
                        if (d - i >= 2)
                            Qb(i, j) += Qb(d, e) * Qm(i + 1, d - 1) * exp(-(a1 + 2 * a2 + (j - e - 1) * a3) / RT);
                    }
            }

            // Qm recursion
            for (d = i; d <= j - 4; d++)    // loop over all possible rightmost basepairs (d,e)
                for (e = d + 4; e <= j; ++e) {
                    Qm(i, j) += Qb(d, e) * exp(-(a2 + a3 * (d - i + j - e)) / RT);
                    if (d - i > 0) Qm(i, j) += Qb(d, e) * Qm(i, d - 1) * exp(-(a2 + a3 * (j - e)) / RT);
                }

            // Q recursion
            Q(i, j) = 1.0;                  // exp(-Gempty/RT) (no basepairs between i and j)
            for (d = i; d <= j - 4; d++)    // loop over all possible rightmost basepairs (d,e)
                for (e = d + 4; e <= j; ++e)
                    if (d - i > 0)
                        Q(i, j) += Q(i, d - 1) * Qb(d, e);
                    else
                        Q(i, j) += Qb(d, e);
            // cout << " = " << Q(i, j) << endl;
        }
    }

    // Print Q(1,n_)
    cout << "\t\t>Partition function is " << Q(0, n_ - 1) << endl;

    vector<MatrixXf> partition_functions = vector<MatrixXf>(3);
    partition_functions[0]               = Q;
    partition_functions[1]               = Qb;
    partition_functions[2]               = Qm;
    return partition_functions;
}


vector<MatrixXf> RNA::compute_partition_function_noPK_ON3(void)
{
    // This is the o(N³) algorithm from Dirks & Pierce, 2003
    // Only computes Q, Qb, Qm, Qs, Qms, Qx, Qx1 and Qx2
    // Gmultiloop is approximated by a1 + k*a2 + u*a3 (a,b,c in McCaskill)
    // Pseudoknots supposed impossible in the structure
    // Uses "fastGIL" to compute IL contributions to Qg in O(N⁵).

    float RT = kB * AVOGADRO * (ZERO_C_IN_KELVIN + 37.0);
    float a1 = nrjp_.a1;
    float a2 = nrjp_.a2;
    float a3 = nrjp_.a3;

    // O(8N²) space
    MatrixXf Q   = MatrixXf::Zero(n_, n_);
    MatrixXf Qb  = MatrixXf::Zero(n_, n_);
    MatrixXf Qm  = MatrixXf::Zero(n_, n_);
    MatrixXf Qs  = MatrixXf::Zero(n_, n_);
    MatrixXf Qms = MatrixXf::Zero(n_, n_);
    MatrixXf Qx  = MatrixXf::Zero(n_, n_);
    MatrixXf Qx1 = MatrixXf::Zero(n_, n_);
    MatrixXf Qx2 = MatrixXf::Zero(n_, n_);
    int      i, j, d, e, l, s, L1, L2;
    // case where l=2
    for (i = 0; i < int(n_) - 1; i++) Q(i, i + 1) = 1.0;    // exp(- Gempty / RT) = exp(0) = 1.0
    // cases where l=3, l=4, no hairpins possible
    for (l = 3; l < 5; l++)
        for (i = 0; i <= int(n_) - l; i++) Q(i, i + l - 1) = 1.0;    // exp(-Gempty/RT) (no basepairs between i and j)
    // Cases considering subsequences of growing sizes from 5 until n_
    for (l = 5; l <= int(n_); l++) {
        // cout << "\t\tComputing subsequences of size " << l << endl;
        Qx  = Qx1;
        Qx1 = Qx2;
        Qx2.setZero();
#pragma omp parallel for private(j, d, e, L1, L2, s)
        for (i = 0; i < int(n_) - l + 1; i++) {
            j = i + l - 1;    // Consider the subsequence [i,j] of length l

            // Qx definition
            if (l >= 15)    // subsequences not added to Qb as a special cases. 4(L1) + 4(L2) + 1(i) + 1(j) + 1(d) + 1(e) + 3 = 15
            {
                d  = i + 5;    // fixed L1=4, L2>=4
                L1 = d - i - 1;
                for (e = d + 4, L2 = j - e - 1; e <= j - 5; e++, L2 = j - e - 1)
                    Qx(i, L1 + L2) += Qb(d, e) * exp(-(GIL_asymmetry(L1, L2) + GIL_mismatch(d, e, d + 1, e - 1)) / RT);
                e  = j - 5;    // fixed L2=4, L1>=4
                L2 = j - e - 1;
                for (d = i + 6, L1 = d - i - 1; d <= e - 4; d++, L1 = d - i - 1)
                    Qx(i, L1 + L2) += Qb(d, e) * exp(-(GIL_asymmetry(L1, L2) + GIL_mismatch(d, e, d + 1, e - 1)) / RT);
                if (i > 0 and j != int(n_))    // compute Qx2 for later use
                    for (s = 8; s <= l - 7; s++) Qx2(i - 1, s + 2) = Qx(i, s) * exp(-(Gloop(s + 2) - Gloop(s)) / RT);
            }
            // Qb recursion
            Qb(i, j) = exp(-GHL(i, j) / RT);    // Hairpin loop case
            for (s = 8; s <= l - 7; s++)
                Qb(i, j) += Qx(i, s) * exp(-GIL_mismatch(i, j, i + 1, j - 1) / RT);    // Convert Qx to Qb (L1 and L2 > 4)
            for (d = i + 1; d <= i + 4; d++)    // Add small inextensible IL terms to Qb as special cases
                for (e = max(d + 4, j - 4); e <= j - 1; e++) Qb(i, j) += Qb(d, e) * exp(-GIL(i, d, e, j, false) / RT);
            for (d = i + 1; d <= i + 4; d++)    // Add bulges and large asymmetric loops as special cases where L1 = 0,1,2,3 and L2>=4
                for (e = d + 4; e <= j - 5; e++) Qb(i, j) += Qb(d, e) * exp(-GIL(i, d, e, j, false) / RT);
            for (e = j - 4; e <= j - 1; e++)    // Add bulges and large asymmetric loops as special cases where L2 = 0,1,2,3 and L1>=4
                for (d = i + 5; d <= e - 4; d++) Qb(i, j) += exp(-GIL(i, d, e, j, false) / RT) * Qb(d, e);
            for (d = i + 6; d <= j - 5; d++)    // Multiloop case
                Qb(i, j) += Qm(i + 1, d - 1) * Qms(d, j - 1) * exp(-(a1 + a2) / RT);

            // Qs recursion : all possible rightmost basepairs involving i
            for (d = i + 4; d <= j; d++) Qs(i, j) += Qb(i, d);

            // Qms recursion : all possible rightmost basepairs involving i, but inside a multiloop
            for (d = i + 4; d <= j; d++) Qms(i, j) += Qb(i, d) * exp(-(a2 + a3 * (j - d)) / RT);

            // Qm recursion
            for (d = i; d <= j - 4; d++) {    // loop over all possible rightmost basepairs (d,e)
                Qm(i, j) += Qms(d, j) * exp(-a3 * (d - i) / RT);
                if (d - i > 0) Qm(i, j) += Qms(d, j) * Qm(i, d - 1);
            }

            // Q recursion
            Q(i, j) = 1.0;    // exp(-Gempty/RT), if empty (no basepairs between i and j)
            for (d = i; d <= j - 4; d++)
                if (d - i > 0)
                    Q(i, j) += Q(i, d - 1) * Qs(d, j);
                else
                    Q(i, j) += Qs(d, j);
        }
    }

    // Print Q(1,n_)
    cout << "\t\t>Partition function (fast computed) is " << Q(0, n_ - 1) << endl;

    vector<MatrixXf> partition_functions = vector<MatrixXf>(3);
    partition_functions[0]               = Q;
    partition_functions[1]               = Qb;
    partition_functions[2]               = Qm;
    return partition_functions;
}

pair<vector<MatrixXf>, vector<tensorN4>> RNA::compute_partition_function_PK_ON5(void)
{
    // This is the o(N⁵) algorithm from Dirks & Pierce, 2003
    // Only computes Q, Qb, Qm, Qp and Qz
    // Gmultiloop is approximated by a1 + k*a2 + u*a3 (a,b,c in McCaskill)
    // Uses "fastGIL" to compute certain contributions to Qg in O(N⁵).

    cerr << endl
         << endl
         << "/!\\You are using the fast O(n⁵) computation of the partition function, which is an unfinished method. "
            "Your "
            "results will be wrong !! /!\\"
         << endl
         << endl
         << endl;

    float RT  = kB * AVOGADRO * (ZERO_C_IN_KELVIN + 37.0);
    float a1  = nrjp_.a1;
    float a2  = nrjp_.a2;
    float a3  = nrjp_.a3;
    float b1  = nrjp_.pk_penalty;
    float b1m = nrjp_.pk_multiloop_penalty;
    float b1p = nrjp_.pk_pk_penalty;
    float b2  = nrjp_.pk_paired_penalty;
    float b3  = nrjp_.pk_unpaired_penalty;

    // O(8N⁴ + 5N²) space
    MatrixXf Q  = MatrixXf::Zero(n_, n_);
    MatrixXf Qb = MatrixXf::Zero(n_, n_);
    MatrixXf Qm = MatrixXf::Zero(n_, n_);
    MatrixXf Qp = MatrixXf::Zero(n_, n_);
    MatrixXf Qz = MatrixXf::Zero(n_, n_);
    tensorN4 Qg(n_, n_, n_, n_);
    tensorN4 Qgl(n_, n_, n_, n_);
    tensorN4 Qgr(n_, n_, n_, n_);
    tensorN4 Qgls(n_, n_, n_, n_);
    tensorN4 Qgrs(n_, n_, n_, n_);
    tensorN4 Qx(n_, n_, n_, n_);
    tensorN4 Qx1(n_, n_, n_, n_);
    tensorN4 Qx2(n_, n_, n_, n_);
    Qg.setZero();
    Qgl.setZero();
    Qgr.setZero();
    Qgls.setZero();
    Qgrs.setZero();
    Qx.setZero();
    Qx1.setZero();
    Qx2.setZero();
    int   l, i, j, d, e, f, c;
    float Grecursion;
    for (i = 1; i < int(n_); i++) Q(i, i - 1) = Qz(i, i - 1) = 1.0;
    for (l = 1; l <= int(n_); l++)    // subsequences of increasing length
    {
        cout << "\t\t\tmeasuring sub-loops of length " << l << ".\n";
        Qx  = Qx1;
        Qx1 = Qx2;
        Qx2.setZero();

        // parallel for private(d, e, f, c, Grecursion)
        for (i = 0; i < int(n_) - l + 1; i++)    // define a subsequence [i,j] of length l
        {
            cout << "\t\t\tscanning (" << i << ',' << j << "), length = " << l << ":\t";
            j = i + l - 1;

            // Qb recursion
            if (allowed_basepair(i, j)) {
                Qb(i, j) = exp(-GHL(i, j) / RT);
                for (d = i + 1; d <= j - 5; d++)    // loop over all possible rightmost basepairs (d,e)
                    for (e = d + 4; e <= j - 1; e++) {
                        if (allowed_basepair(d, e)) {
                            Qb(i, j) += exp(-GIL(i, d, e, j, true) / RT) * Qb(d, e);
                            if (d >= i + 6 and is_wc_basepair(d, e) and is_wc_basepair(i, j))
                                Qb(i, j) += Qm(i + 1, d - 1) * Qb(d, e) * exp(-(a1 + 2 * a2 + (j - e - 1) * a3) / RT);
                        }
                    }
                if (is_wc_basepair(i, j))
                    for (d = i + 1; d <= j - 9; d++)    // loop over all possible rightmost pseudoknots filling [d,e]
                        for (e = d + 8; e <= j - 1; e++) {
                            Grecursion = a1 + b1m + 3 * a2 + (j - e - 1) * a3;
                            Qb(i, j) += exp(-(Grecursion + a3 * (d - i - 1)) / RT) * Qp(d, e);
                            Qb(i, j) += Qm(i + 1, d - 1) * Qp(d, e) * exp(-Grecursion / RT);
                        }

                // Qg recursion
                Qg(i, i, j, j) = 1.0;
                for (d = i + 1; d <= j - 5; d++)
                    for (e = d + 4; e <= j - 1; e++)
                        if (allowed_basepair(d, e)) Qg(i, d, e, j) += exp(-GIL(i, d, e, j, true) / RT);
            }
            // fastGIL(i, j, Qg, Qx, Qx2);
            if (allowed_basepair(i, j) and is_wc_basepair(i, j)) {
                for (d = i + 6; d <= j - 5; d++)    // multiloop left
                    for (e = d + 4; e <= j - 1; e++)
                        if (allowed_basepair(d, e) and is_wc_basepair(d, e))
                            Qg(i, d, e, j) += Qm(i + 1, d - 1) * exp(-(a1 + 2 * a2 + (j - e - 1) * a3) / RT);
                for (d = i + 1; d <= j - 10; d++)    // multiloop right
                    for (e = d + 4; e <= j - 6; e++)
                        if (allowed_basepair(d, e) and is_wc_basepair(d, e))
                            Qg(i, d, e, j) += exp(-(a1 = 2 * a2 + (d - i - 1) * a3) / RT) * Qm(e + 1, j - 1);
                for (d = i + 6; d <= j - 10; d++)    // multiloop both sides
                    for (e = d + 4; e <= j - 6; e++)
                        if (allowed_basepair(d, e) and is_wc_basepair(d, e))
                            Qg(i, d, e, j) += Qm(i + 1, d - 1) * exp(-(a1 + 2 * a2) / RT) * Qm(e + 1, j - 1);
                for (d = i + 7; d <= j - 6; d++)    // IL + multiloop left
                    for (e = d + 4; e <= j - 2; e++)
                        if (allowed_basepair(d, e))
                            for (f = e + 1; f <= j - 1; f++)
                                Qg(i, d, e, j) += Qgls(i + 1, d, e, f) * exp(-(a1 + a2 + (j - f - 1) * a3) / RT);
                for (d = i + 2; d <= j - 11; d++)    // IL + multiloop right
                    for (e = d + 4; e <= j - 7; e++)
                        if (allowed_basepair(d, e))
                            for (c = i + 1; c <= d - 1; c++)
                                Qg(i, d, e, j) += exp(-(a1 + a2 + (c - i - 1) * a3) / RT) * Qgrs(c, d, e, j - 1);
                for (d = i + 7; d <= j - 11; d++)    // IL + multiloop both sides
                    for (e = d + 4; e <= j - 7; e++)
                        if (allowed_basepair(d, e))
                            for (c = i + 6; c <= d - 1; c++)
                                Qg(i, d, e, j) += Qm(i + 1, c - 1) * Qgrs(c, d, e, j - 1) * exp(-(a1 + a2) / RT);
            }
            // Qgls & Qgrs recursions
            for (c = i + 5; c <= j - 6; c++)
                if (allowed_basepair(c, j) and is_wc_basepair(c, j))
                    for (d = c + 1; d <= j - 5; d++)
                        for (e = d + 4; e <= j - 1; e++)
                            if (allowed_basepair(d, e))
                                Qgls(i, d, e, j) += exp(-a2 / RT) * Qm(i, c - 1) * Qg(c, d, e, j);
            for (d = i + 1; d <= j - 10; d++)
                for (e = d + 4; e <= j - 6; e++)
                    if (allowed_basepair(d, e))
                        for (f = e + 1; f <= j - 5; f++)
                            if (allowed_basepair(i, f) and is_wc_basepair(i, f))
                                Qgrs(i, d, e, j) += Qg(i, d, e, f) * Qm(f + 1, j) * exp(-a2 / RT);

            // Qgl, Qgr recursions
            for (d = i + 1; d <= j - 5; d++)
                for (f = d + 4; f <= j - 1; f++)
                    if (allowed_basepair(d, f) and is_wc_basepair(d, f))
                        for (e = d; e <= f - 3; e++) Qgl(i, e, f, j) += Qg(i, d, f, j) * Qz(d + 1, e) * exp(-b2 / RT);
            for (d = i + 1; d <= j - 4; d++)
                for (e = d + 3; e <= j - 1; e++)
                    for (f = e; f <= j - 1; f++) Qgr(i, d, e, j) += Qgl(i, d, f, j) * Qz(e, f - 1);

            // Qp recursion
            for (d = i + 2; d <= j - 4; d++)
                for (e = max(d + 2, i + 5); e <= j - 3; e++)
                    for (f = e + 1; f <= j - 2; f++) Qp(i, j) += Qgl(i, d - 1, e, f) * Qgr(d, e - 1, f + 1, j);

            // Q, Qm, Qz recursions
            Q(i, j) = 1.0;    // if empty, no basepairs between (i,j)
            if (i and j != int(n_) - 1) Qz(i, j) = exp(-(b3 * (j - i + 1)) / RT);
            for (d = i; d <= j - 4; d++)    // all possible rightmost pairs (d,e)
                for (e = d + 4; e <= j; e++)
                    if (allowed_basepair(d, e) and is_wc_basepair(d, e)) {
                        Q(i, j) += Q(i, d - 1) * Qb(d, e);
                        if (i and j != int(n_) - 1) {
                            Qm(i, j) += exp(-(a2 + (d - i + j - e) * a3) / RT) * Qb(d, e);
                            if (d >= i + 5) Qm(i, j) += Qm(i, d - 1) * Qb(d, e) * exp(-(a2 + (j - e) * a3) / RT);
                            Qz(i, j) += Qz(i, d - 1) * Qb(d, e) * exp(-(b2 + (j - e) * b3) / RT);
                        }
                    }
            for (d = i; d <= j - 8; d++)    // for all possible rightmost pseudoknots filling (d,e)
                for (e = d + 8; e <= j; e++) {
                    Q(i, j) += Q(i, d - 1) * Qp(d, e) * exp(-b1 / RT);
                    if (i and j != int(n_) - 1) {
                        Qm(i, j) += exp(-(b1m + 2 * a2 + (d - i + j - e) * a3) / RT) * Qp(d, e);
                        if (d >= i + 5) Qm(i, j) += Qm(i, d - 1) * Qp(d, e) * exp(-(b1m + 2 * a2 + (j - e) * a3) / RT);
                        Qz(i, j) += Qz(i, d - 1) * Qp(d, e) * exp(-(b1p + 2 * b2 + (j - e) * b3) / RT);
                    }
                }
            cout << Q(i, j) << endl;
        }
    }
    vector<MatrixXf> partition_functions      = vector<MatrixXf>(5);
    partition_functions[0]                    = Q;
    partition_functions[1]                    = Qb;
    partition_functions[2]                    = Qm;
    partition_functions[3]                    = Qp;
    partition_functions[4]                    = Qz;
    vector<tensorN4> partition_functions_next = vector<tensorN4>(5);
    partition_functions_next[0]               = Qg;
    partition_functions_next[1]               = Qgl;
    partition_functions_next[2]               = Qgr;
    partition_functions_next[3]               = Qgls;
    partition_functions_next[4]               = Qgrs;
    pair<vector<MatrixXf>, vector<tensorN4>> results;
    results = make_pair(partition_functions, partition_functions_next);
    return results;
}

pair<vector<MatrixXf>, vector<tensorN4>> RNA::compute_partition_function_PK_ON8(void)
{
    // This is the o(N⁸) algorithm from Dirks & Pierce, 2003
    // Computes Q, Qb, Qm, Qp and Qz
    // Gmultiloop is approximated by a1 + k*a2 + u*a3 (a,b,c in McCaskill)

    cerr << endl
         << endl
         << "/!\\You are using the slow O(n⁸) computation of the partition function, which is an unfinished method. "
            "Your "
            "results will be wrong !! /!\\"
         << endl
         << endl
         << endl;

    float RT  = kB * AVOGADRO * (ZERO_C_IN_KELVIN + 37.0);
    float a1  = nrjp_.a1;
    float a2  = nrjp_.a2;
    float a3  = nrjp_.a3;
    float b1  = nrjp_.pk_penalty;
    float b1m = nrjp_.pk_multiloop_penalty;
    float b1p = nrjp_.pk_pk_penalty;
    float b2  = nrjp_.pk_paired_penalty;
    float b3  = nrjp_.pk_unpaired_penalty;

    // O(8N⁴ + 5N²) space
    MatrixXf Q  = MatrixXf::Zero(n_, n_);
    MatrixXf Qb = MatrixXf::Zero(n_, n_);
    MatrixXf Qm = MatrixXf::Zero(n_, n_);
    MatrixXf Qp = MatrixXf::Zero(n_, n_);
    MatrixXf Qz = MatrixXf::Zero(n_, n_);
    tensorN4 Qg(n_, n_, n_, n_);
    tensorN4 Qgl(n_, n_, n_, n_);
    tensorN4 Qgr(n_, n_, n_, n_);
    tensorN4 Qgls(n_, n_, n_, n_);
    tensorN4 Qgrs(n_, n_, n_, n_);
    tensorN4 Qx(n_, n_, n_, n_);
    tensorN4 Qx1(n_, n_, n_, n_);
    tensorN4 Qx2(n_, n_, n_, n_);
    Qg.setZero();
    Qgl.setZero();
    Qgr.setZero();
    Qgls.setZero();
    Qgrs.setZero();
    Qx.setZero();
    Qx1.setZero();
    Qx2.setZero();
    int   l, i, j, d, e, f, c;
    float Grecursion;
    for (i = 1; i < int(n_); i++) Q(i, i - 1) = Qz(i, i - 1) = 1.0;
    for (l = 1; l <= int(n_); l++)    // subsequences of increasing length
    {
        cout << "\t\t\tmeasuring sub-loops of length " << l << ".\n";
        Qx  = Qx1;
        Qx1 = Qx2;
        Qx2.setZero();

        // parallel for private(d, e, f, c, Grecursion)
        for (i = 0; i < int(n_) - l + 1; i++)    // define a subsequence [i,j] of length l
        {
            cout << "\t\t\tscanning (" << i << ',' << j << "), length = " << l << ":\t";
            j = i + l - 1;

            // Qb recursion
            if (allowed_basepair(i, j)) {
                Qb(i, j) = exp(-GHL(i, j) / RT);
                for (d = i + 1; d <= j - 5; d++)    // loop over all possible rightmost basepairs (d,e)
                    for (e = d + 4; e <= j - 1; e++) {
                        if (allowed_basepair(d, e)) {
                            Qb(i, j) += exp(-GIL(i, d, e, j, true) / RT) * Qb(d, e);
                            if (d >= i + 6 and is_wc_basepair(d, e) and is_wc_basepair(i, j))
                                Qb(i, j) += Qm(i + 1, d - 1) * Qb(d, e) * exp(-(a1 + 2 * a2 + (j - e - 1) * a3) / RT);
                        }
                    }
                if (is_wc_basepair(i, j))
                    for (d = i + 1; d <= j - 9; d++)    // loop over all possible rightmost pseudoknots filling [d,e]
                        for (e = d + 8; e <= j - 1; e++) {
                            Grecursion = a1 + b1m + 3 * a2 + (j - e - 1) * a3;
                            Qb(i, j) += exp(-(Grecursion + a3 * (d - i - 1)) / RT) * Qp(d, e);
                            Qb(i, j) += Qm(i + 1, d - 1) * Qp(d, e) * exp(-Grecursion / RT);
                        }

                // Qg recursion
                Qg(i, i, j, j) = 1.0;
                for (d = i + 1; d <= j - 5; d++)
                    for (e = d + 4; e <= j - 1; e++)
                        if (allowed_basepair(d, e)) Qg(i, d, e, j) += exp(-GIL(i, d, e, j, true) / RT);
            }
            // fastGIL(i, j, Qg, Qx, Qx2);
            if (allowed_basepair(i, j) and is_wc_basepair(i, j)) {
                for (d = i + 6; d <= j - 5; d++)    // multiloop left
                    for (e = d + 4; e <= j - 1; e++)
                        if (allowed_basepair(d, e) and is_wc_basepair(d, e))
                            Qg(i, d, e, j) += Qm(i + 1, d - 1) * exp(-(a1 + 2 * a2 + (j - e - 1) * a3) / RT);
                for (d = i + 1; d <= j - 10; d++)    // multiloop right
                    for (e = d + 4; e <= j - 6; e++)
                        if (allowed_basepair(d, e) and is_wc_basepair(d, e))
                            Qg(i, d, e, j) += exp(-(a1 = 2 * a2 + (d - i - 1) * a3) / RT) * Qm(e + 1, j - 1);
                for (d = i + 6; d <= j - 10; d++)    // multiloop both sides
                    for (e = d + 4; e <= j - 6; e++)
                        if (allowed_basepair(d, e) and is_wc_basepair(d, e))
                            Qg(i, d, e, j) += Qm(i + 1, d - 1) * exp(-(a1 + 2 * a2) / RT) * Qm(e + 1, j - 1);
                for (d = i + 7; d <= j - 6; d++)    // IL + multiloop left
                    for (e = d + 4; e <= j - 2; e++)
                        if (allowed_basepair(d, e))
                            for (f = e + 1; f <= j - 1; f++)
                                Qg(i, d, e, j) += Qgls(i + 1, d, e, f) * exp(-(a1 + a2 + (j - f - 1) * a3) / RT);
                for (d = i + 2; d <= j - 11; d++)    // IL + multiloop right
                    for (e = d + 4; e <= j - 7; e++)
                        if (allowed_basepair(d, e))
                            for (c = i + 1; c <= d - 1; c++)
                                Qg(i, d, e, j) += exp(-(a1 + a2 + (c - i - 1) * a3) / RT) * Qgrs(c, d, e, j - 1);
                for (d = i + 7; d <= j - 11; d++)    // IL + multiloop both sides
                    for (e = d + 4; e <= j - 7; e++)
                        if (allowed_basepair(d, e))
                            for (c = i + 6; c <= d - 1; c++)
                                Qg(i, d, e, j) += Qm(i + 1, c - 1) * Qgrs(c, d, e, j - 1) * exp(-(a1 + a2) / RT);
            }
            // Qgls & Qgrs recursions
            for (c = i + 5; c <= j - 6; c++)
                if (allowed_basepair(c, j) and is_wc_basepair(c, j))
                    for (d = c + 1; d <= j - 5; d++)
                        for (e = d + 4; e <= j - 1; e++)
                            if (allowed_basepair(d, e))
                                Qgls(i, d, e, j) += exp(-a2 / RT) * Qm(i, c - 1) * Qg(c, d, e, j);
            for (d = i + 1; d <= j - 10; d++)
                for (e = d + 4; e <= j - 6; e++)
                    if (allowed_basepair(d, e))
                        for (f = e + 1; f <= j - 5; f++)
                            if (allowed_basepair(i, f) and is_wc_basepair(i, f))
                                Qgrs(i, d, e, j) += Qg(i, d, e, f) * Qm(f + 1, j) * exp(-a2 / RT);

            // Qgl, Qgr recursions
            for (d = i + 1; d <= j - 5; d++)
                for (f = d + 4; f <= j - 1; f++)
                    if (allowed_basepair(d, f) and is_wc_basepair(d, f))
                        for (e = d; e <= f - 3; e++) Qgl(i, e, f, j) += Qg(i, d, f, j) * Qz(d + 1, e) * exp(-b2 / RT);
            for (d = i + 1; d <= j - 4; d++)
                for (e = d + 3; e <= j - 1; e++)
                    for (f = e; f <= j - 1; f++) Qgr(i, d, e, j) += Qgl(i, d, f, j) * Qz(e, f - 1);

            // Qp recursion
            for (d = i + 2; d <= j - 4; d++)
                for (e = max(d + 2, i + 5); e <= j - 3; e++)
                    for (f = e + 1; f <= j - 2; f++) Qp(i, j) += Qgl(i, d - 1, e, f) * Qgr(d, e - 1, f + 1, j);

            // Q, Qm, Qz recursions
            Q(i, j) = 1.0;    // if empty, no basepairs between (i,j)
            if (i and j != int(n_) - 1) Qz(i, j) = exp(-(b3 * (j - i + 1)) / RT);
            for (d = i; d <= j - 4; d++)    // all possible rightmost pairs (d,e)
                for (e = d + 4; e <= j; e++)
                    if (allowed_basepair(d, e) and is_wc_basepair(d, e)) {
                        Q(i, j) += Q(i, d - 1) * Qb(d, e);
                        if (i and j != int(n_) - 1) {
                            Qm(i, j) += exp(-(a2 + (d - i + j - e) * a3) / RT) * Qb(d, e);
                            if (d >= i + 5) Qm(i, j) += Qm(i, d - 1) * Qb(d, e) * exp(-(a2 + (j - e) * a3) / RT);
                            Qz(i, j) += Qz(i, d - 1) * Qb(d, e) * exp(-(b2 + (j - e) * b3) / RT);
                        }
                    }
            for (d = i; d <= j - 8; d++)    // for all possible rightmost pseudoknots filling (d,e)
                for (e = d + 8; e <= j; e++) {
                    Q(i, j) += Q(i, d - 1) * Qp(d, e) * exp(-b1 / RT);
                    if (i and j != int(n_) - 1) {
                        Qm(i, j) += exp(-(b1m + 2 * a2 + (d - i + j - e) * a3) / RT) * Qp(d, e);
                        if (d >= i + 5) Qm(i, j) += Qm(i, d - 1) * Qp(d, e) * exp(-(b1m + 2 * a2 + (j - e) * a3) / RT);
                        Qz(i, j) += Qz(i, d - 1) * Qp(d, e) * exp(-(b1p + 2 * b2 + (j - e) * b3) / RT);
                    }
                }
            cout << Q(i, j) << endl;
        }
    }
    vector<MatrixXf> partition_functions      = vector<MatrixXf>(5);
    partition_functions[0]                    = Q;
    partition_functions[1]                    = Qb;
    partition_functions[2]                    = Qm;
    partition_functions[3]                    = Qp;
    partition_functions[4]                    = Qz;
    vector<tensorN4> partition_functions_next = vector<tensorN4>(5);
    partition_functions_next[0]               = Qg;
    partition_functions_next[1]               = Qgl;
    partition_functions_next[2]               = Qgr;
    partition_functions_next[3]               = Qgls;
    partition_functions_next[4]               = Qgrs;
    pair<vector<MatrixXf>, vector<tensorN4>> results;
    results = make_pair(partition_functions, partition_functions_next);
    return results;
}

MatrixXf RNA::compute_posterior_noPK_ON4(bool fast)
{
    vector<MatrixXf> partition_functions(0);
    if (fast)
        partition_functions = compute_partition_function_noPK_ON3();
    else
        partition_functions = compute_partition_function_noPK_ON4();
    MatrixXf& Q  = partition_functions[0];
    MatrixXf& Qb = partition_functions[1];
    MatrixXf& Qm = partition_functions[2];
    float     RT = kB * AVOGADRO * (ZERO_C_IN_KELVIN + 37.0);
    float     a1 = nrjp_.a1;
    float     a2 = nrjp_.a2;
    float     a3 = nrjp_.a3;

    // O(3N²) space
    MatrixXf P  = MatrixXf::Zero(n_, n_);
    MatrixXf Pb = MatrixXf::Zero(n_, n_);
    MatrixXf Pm = MatrixXf::Zero(n_, n_);
    int      l, i, j, d, e;
    float    dP;

    P(0, n_ - 1) = 1.0;    // probability of recursing to the entire strand is 1
    for (l = n_; l > 0; l--) {
        #pragma omp parallel for private(j, d, e, dP)
        for (i = 0; i <= int(n_) - l; i++) {
            j = i + l - 1;

            // printMatrix(Pb);

            // P, Pm recursion
            for (d = i; d <= j - 4; d++) {
                for (e = d + 4; e <= j; e++) {
                    if (d > i) {
                        dP = P(i, j) * Q(i, d - 1) * Qb(d, e) / Q(i, j);
                        P(i, d - 1) += dP;
                    } else {
                        dP = P(i, j) * Qb(d, e) / Q(i, j);
                    }
                    Pb(d, e) += dP;

                    Pb(d, e) += Pm(i, j) * exp(-(a2 + a3 * (d - i + j - e) / RT)) * Qb(d, e) / Qm(i, j);
                    if (d > i) {
                        dP = Pm(i, j) * Qm(i, d - 1) * Qb(d, e) * exp(-(a2 + a3 * (j - e)) / RT) / Qm(i, j);
                        Pm(i, d - 1) += dP;
                    } else {
                        dP = Pm(i, j) * Qb(d, e) * exp(-(a2 + a3 * (j - e)) / RT) / Qm(i, j);
                    }
                    Pb(d, e) += dP;
                    assert(!std::isnan(dP));
                }
            }

            // Pb recursion
            for (d = i + 1; d <= j - 5; d++)
                for (e = d + 4; e <= j - 1; e++) {
                    if (Qb(i, j) > 0) {
                        Pb(d, e) += Pb(i, j) * Qb(d, e) * exp(-GIL(i, d, e, j, false) / RT) / Qb(i, j);
                        dP = Pb(i, j) * Qm(i + 1, d - 1) * Qb(d, e) * exp(-(a1 + 2 * a2 + (j - e - 1) * a3) / RT) / Qb(i, j);
                        Pm(i + 1, d - 1) += dP;
                        Pb(d, e) += dP;
                        assert(!std::isnan(dP));
                    }
                }
        }
    }
    return Pb;
}

MatrixXf RNA::compute_posterior_PK_ON6(bool fast)
{
    if (fast) {
        pair<vector<MatrixXf>, vector<tensorN4>> results                  = compute_partition_function_PK_ON5();
        vector<MatrixXf>                         partition_functions      = results.first;
        vector<tensorN4>                         partition_functions_next = results.second;

        MatrixXf& Q    = partition_functions[0];
        MatrixXf& Qb   = partition_functions[1];
        MatrixXf& Qm   = partition_functions[2];
        MatrixXf& Qp   = partition_functions[3];
        MatrixXf& Qz   = partition_functions[4];
        tensorN4& Qg   = partition_functions_next[0];
        tensorN4& Qgl  = partition_functions_next[1];
        tensorN4& Qgr  = partition_functions_next[2];
        tensorN4& Qgls = partition_functions_next[3];
        tensorN4& Qgrs = partition_functions_next[4];
    } else {
        pair<vector<MatrixXf>, vector<tensorN4>> results                  = compute_partition_function_PK_ON8();
        vector<MatrixXf>                         partition_functions      = results.first;
        vector<tensorN4>                         partition_functions_next = results.second;

        MatrixXf& Q    = partition_functions[0];
        MatrixXf& Qb   = partition_functions[1];
        MatrixXf& Qm   = partition_functions[2];
        MatrixXf& Qp   = partition_functions[3];
        MatrixXf& Qz   = partition_functions[4];
        tensorN4& Qg   = partition_functions_next[0];
        tensorN4& Qgl  = partition_functions_next[1];
        tensorN4& Qgr  = partition_functions_next[2];
        tensorN4& Qgls = partition_functions_next[3];
        tensorN4& Qgrs = partition_functions_next[4];
    }
    MatrixXf Pb = MatrixXf::Zero(n_, n_);
    return Pb;
}

void RNA::compute_basepair_probabilities(bool pk, bool fast)
{
    if (pk)
        pij_ = compute_posterior_PK_ON6(fast);
    else
        pij_ = compute_posterior_noPK_ON4(fast);
}
