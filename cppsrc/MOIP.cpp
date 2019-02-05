#include "MOIP.h"
#include <algorithm>
#include <boost/format.hpp>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using std::abs;
using std::cerr;
using std::cout;
using std::endl;
using std::make_pair;
using std::vector;

unsigned getNumConstraints(IloModel& m)
{
    unsigned           count = 0;
    IloModel::Iterator iter(m);
    while (iter.ok()) {
        if ((*iter).asConstraint().getImpl()) {
            ++count;
        }
        ++iter;
    }
    return count;
}

MOIP::MOIP(const RNA& rna, const vector<Motif>& insertionSites, float pthreshold, bool verbose)
: verbose_{verbose}, rna_(rna), insertion_sites_(insertionSites), theta_{pthreshold}
{

    if (verbose_) rna_.print_basepair_p_matrix(theta_);

    if (verbose_) cout << "defining problem decision variables..." << endl;
    basepair_dv_  = IloNumVarArray(env_);
    insertion_dv_ = IloNumVarArray(env_);

    // Add the y^u_v decision variables
    if (verbose_) cout << "\t>Legal basepairs : ";
    uint u, v, c = 0;
    index_of_yuv_ = vector<vector<size_t>>(rna_.get_RNA_length() - 6, vector<size_t>(0));
    for (u = 0; u < rna_.get_RNA_length() - 6; u++)
        for (v = u + 4; v < rna_.get_RNA_length(); v++)    // A basepair is possible iff v > u+3
            if (rna_.get_pij(u, v) > theta_) {
                if (verbose_) cout << u << '-' << v << " ";
                index_of_yuv_[u].push_back(c);
                c++;
                char name[15];
                sprintf(name, "y%d,%d", u, v);
                basepair_dv_.add(IloNumVar(env_, 0, 1, IloNumVar::Bool, name));    // A boolean whether u and v are paired
            } else {
                index_of_yuv_[u].push_back(rna_.get_RNA_length() * rna_.get_RNA_length() + 1);
            }
    if (verbose_) cout << endl;

    // Add the Cx,i,p decision variables
    if (verbose_) cout << "\t>Candidate motif insertion sites : " << endl;
    index_of_first_components.reserve(insertionSites.size());
    index_of_Cxip_.reserve(insertionSites.size());
    size_t i = 0;
    for (const Motif m : insertionSites) {
        if (verbose_) cout << "\t\t" << m.pos_string() << endl;
        index_of_first_components.push_back(i);
        index_of_Cxip_.push_back(vector<size_t>(0));
        for (const Component cmp : m.comp) {
            index_of_Cxip_.back().push_back(i);
            i++;
            char name[20];
            sprintf(
            name,
            "C%d,%d-%d",
            static_cast<int>(index_of_Cxip_.size() - 1),
            static_cast<int>(index_of_Cxip_.back().size() - 1),
            cmp.pos.first);
            insertion_dv_.add(IloNumVar(env_, 0, 1, IloNumVar::Bool, name));    // A boolean whether component i of motif x is inserted at position p
        }
    }

    if (verbose_) cout << c << " + " << i << " (yuv + Cpxi) decision variables are used." << endl;

    // Adding the problem's constraints
    model_ = IloModel(env_);
    define_problem_constraints();
    if (verbose_) cout << "A total of " << getNumConstraints(model_) << " constraints are used." << endl;

    // Define the motif objective function:
    obj1 = IloExpr(env_);
    for (uint i = 0; i < insertion_sites_.size(); i++) {
        // RNA MoIP style : objective f_1A
        // IloNum n_compo_squared = IloNum(insertion_sites_[i].comp.size() * insertion_sites_[i].comp.size());
        // obj1 += n_compo_squared * insertion_dv_[index_of_first_components[i]];

        // Weighted by the JAR3D score:
        obj1 += IloNum(insertion_sites_[i].score) * insertion_dv_[index_of_first_components[i]];
    }

    // Define the expected accuracy objective function:
    obj2 = IloExpr(env_);
    for (size_t u = 0; u < rna_.get_RNA_length() - 6; u++) {
        for (size_t v = u + 4; v < rna_.get_RNA_length(); v++) {
            if (allowed_basepair(u, v)) obj2 += (IloNum(rna_.get_pij(u, v)) * y(u, v));
        }
    }
}

MOIP::~MOIP() { env_.end(); }



bool MOIP::is_undominated_yet(const SecondaryStructure& s)
{
    for (uint i = 0; i < pareto_.size(); i++) {
        if (pareto_[i] > s) return false;
    }
    return true;
}

SecondaryStructure MOIP::solve_objective(int o, double min, double max)
{
    // Solves one of the objectives, under constraint that the other should be in [min, max]
    if (verbose_)
        cout << "Solving objective function " << o << ", " << min << " <= Obj" << 3 - o << " <= " << max << "..." << endl;

    IloObjective obj;
    IloRange     bounds;

    switch (o) {
    case 1:
        obj    = IloMaximize(env_, obj1);
        bounds = IloRange(env_, min, obj2, max);
        break;
    case 2: obj = IloMaximize(env_, obj2); bounds = IloRange(env_, min, obj1, max);
    }
    model_.add(obj);
    model_.add(bounds);
    IloCplex cplex_ = IloCplex(model_);
    cplex_.setOut(env_.getNullStream());
    // cplex_.exportModel("latestmodel.lp");

    if (!cplex_.solve()) {
        if (verbose_) env_.error() << "\t>Failed to optimize LP: no more solutions to find." << endl;
        return SecondaryStructure(true);
    }

    if (verbose_) {
        cout << "\t>Solution status: " << cplex_.getStatus() << ", with objective " << o << " value "
             << cplex_.getObjValue() << endl;
        cout << "\t>Building secondary structure..." << endl;
    }

    // Build a secondary Structure
    SecondaryStructure best_ss = SecondaryStructure(rna_);
    // if (verbose_) cout << "\t\t>retrieveing motifs inserted in the result secondary structure..." << endl;
    for (size_t i = 0; i < insertion_sites_.size(); i++)
        // A constraint requires that all the components are inserted or none, so testing the first is enough:
        if (cplex_.getValue(insertion_dv_[index_of_first_components[i]]) > 0.5)
            best_ss.insert_motif(insertion_sites_[i]);

    // if (verbose_) cout << "\t\t>retrieving basepairs of the result secondary structure..." << endl;
    for (size_t u = 0; u < rna_.get_RNA_length() - 6; u++)
        for (size_t v = u + 4; v < rna_.get_RNA_length(); v++)
            if (allowed_basepair(u, v))
                if (cplex_.getValue(y(u, v)) > 0.5) best_ss.set_basepair(u, v);

    best_ss.sort();    // order the basepairs in the vector
    best_ss.set_objective_score(2, cplex_.getValue(obj2));
    best_ss.set_objective_score(1, cplex_.getValue(obj1));

    // Forbidding to find best_ss later : add a constraint to model_
    IloExpr c(env_);
    for (uint d = 0; d < insertion_dv_.getSize(); d++)
        if (cplex_.getValue(insertion_dv_[d]) > 0.5)
            c += IloNum(1) - insertion_dv_[d];
        else
            c += insertion_dv_[d];
    for (uint d = 0; d < basepair_dv_.getSize(); d++)
        if (cplex_.getValue(basepair_dv_[d]) > 0.5)
            c += IloNum(1) - basepair_dv_[d];
        else
            c += basepair_dv_[d];
    model_.add(c >= IloNum(1));
    if (verbose_) cout << "\t>adding " << getNumConstraints(model_) << "th constraint " << (c >= IloNum(1)) << endl;

    // Removing the objective from the model_
    model_.remove(obj);
    model_.remove(bounds);

    return best_ss;
}

void MOIP::define_problem_constraints(void)
{
    // ensure there only is 0 or 1 pairing by nucleotide:
    if (verbose_) cout << "\t>ensuring there are at most 1 pairing by nucleotide..." << endl;
    uint u, v, count;
    uint n = rna_.get_RNA_length();
    for (u = 0; u < n; u++) {
        count = 0;
        IloExpr c1(env_);
        for (v = 0; v < u; v++)
            if (allowed_basepair(v, u)) {
                c1 += y(v, u);
                count++;
            }
        for (v = u + 4; v < n; v++)
            if (allowed_basepair(u, v)) {
                c1 += y(u, v);
                count++;
            }
        if (count > 1) {
            model_.add(c1 <= 1);
            if (verbose_) cout << "\t\t" << (c1 <= 1) << endl;
        }
    }
    // forbid lonely basepairs
    if (verbose_) cout << "\t>forbidding lonely basepairs..." << endl;
    for (u = 0; u < n; u++) {
        IloExpr c2(env_);    // for the case where s[u] is paired to s[v], v>u
        count = 0;
        for (v = u; v < n; v++)
            if (allowed_basepair(u - 1, v)) c2 += y(u - 1, v);
        for (v = u + 1; v < n; v++)
            if (allowed_basepair(u, v)) {
                c2 -= y(u, v);
                count++;
            }
        for (v = u + 2; v < n; v++)
            if (allowed_basepair(u + 1, v)) c2 += y(u + 1, v);
        if (count) {
            model_.add(c2 >= 0);
            if (verbose_) cout << "\t\t" << (c2 >= 0) << endl;
        }
    }
    for (v = 2; v < n; v++) {
        IloExpr c2p(env_);    // for the case where s[u] is paired to s[v], v<u
        count = 0;
        for (u = 0; u <= v - 2; u++)
            if (allowed_basepair(u, v - 1)) c2p += y(u, v - 1);
        for (u = 0; u <= v - 1; u++)
            if (allowed_basepair(u, v)) {
                c2p -= y(u, v);
                count++;
            }
        for (u = 0; u <= v; u++)
            if (allowed_basepair(u, v + 1)) c2p += y(u, v + 1);
        if (count) {
            model_.add(c2p >= 0);
            if (verbose_) cout << "\t\t" << (c2p >= 0) << endl;
        }
    }
    // Forbid pairings inside every motif component if included
    if (verbose_) cout << "\t>forbidding basepairs inside included motif's components..." << endl;
    for (size_t i = 0; i < insertion_sites_.size(); i++) {
        Motif& x = insertion_sites_[i];
        for (size_t j = 0; j < x.comp.size(); j++) {
            Component& c = x.comp[j];
            IloExpr    c3(env_);
            IloNum     kxi = IloNum(c.k);
            c3 += (kxi - IloNum(2)) * C(i, j);
            uint count = 0;
            for (u = c.pos.first + 1; u < c.pos.second - 1; u++) {
                for (v = 0; v < n; v++)
                    if (allowed_basepair(u, v)) {
                        c3 += y(u, v);
                        count++;
                    }
            }
            if (count > 1) {
                model_.add(c3 <= (kxi - IloNum(2)));
                if (verbose_) cout << "\t\t";
                // if (verbose_) cout << x.atlas_id << '-' << j << ": ";
                if (verbose_) cout << (c3 <= (kxi - IloNum(2))) << endl;
            }
        }
    }
    // Forbid component overlap
    if (verbose_) cout << "\t>forbidding component overlap..." << endl;
    for (u = 0; u < n; u++) {
        IloExpr c4(env_);
        uint    nterms = 0;
        for (size_t i = 0; i < insertion_sites_.size(); i++) {
            Motif& x = insertion_sites_[i];
            for (size_t j = 0; j < x.comp.size(); j++) {
                Component& c = x.comp[j];
                if (u >= c.pos.first and u <= c.pos.second) {    // Cxip contains u
                    c4 += C(i, j);
                    nterms++;
                }
            }
        }
        if (nterms > 1) {
            model_.add(c4 <= 1);
            if (verbose_) cout << "\t\t" << (c4 <= 1) << endl;
        }
    }
    // Component completeness
    if (verbose_) cout << "\t>ensuring that motives cannot be partially included..." << endl;
    for (size_t i = 0; i < insertion_sites_.size(); i++) {
        Motif& x = insertion_sites_[i];
        if (x.comp.size() == 1)    // This constraint is for multi-component motives.
            continue;
        IloExpr c5(env_);
        IloNum  jm1 = IloNum(x.comp.size() - 1);
        for (size_t j = 1; j < x.comp.size(); j++) {
            c5 += C(i, j);
        }
        model_.add(c5 == jm1 * C(i, 0));
        if (verbose_) cout << "\t\t>motif " << i << " : " << (c5 == jm1 * C(i, 0)) << endl;
    }
    // Force basepairs between the end of a component and the beginning of the next
    if (verbose_) cout << "\t>forcing basepairs between bounds of inserted components..." << endl;
    for (size_t i = 0; i < insertion_sites_.size(); i++) {
        Motif&  x   = insertion_sites_[i];
        IloExpr c6p = IloExpr(env_);
        if (allowed_basepair(x.comp[0].pos.first, x.comp.back().pos.second))
            c6p += y(x.comp[0].pos.first, x.comp.back().pos.second);
        if (verbose_)
            cout << "\t\t" << (C(i, 0) <= c6p) << "\t(" << x.comp[0].pos.first << ',' << x.comp.back().pos.second
                 << (allowed_basepair(x.comp[0].pos.first, x.comp.back().pos.second) > 0 ? ") is allowed" : ") is not allowed")
                 << endl;
        model_.add(C(i, 0) <= c6p);
        if (x.comp.size() == 1)    // This constraint is for multi-component motives.
            continue;
        for (size_t j = 0; j < x.comp.size() - 1; j++) {
            IloExpr c6 = IloExpr(env_);
            if (allowed_basepair(x.comp[j].pos.second, x.comp[j + 1].pos.first))
                c6 += y(x.comp[j].pos.second, x.comp[j + 1].pos.first);
            model_.add(C(i, j) <= c6);
            if (verbose_)
                cout << "\t\t" << (C(i, j) <= c6) << "\t(" << x.comp[j].pos.second << ',' << x.comp[j + 1].pos.first
                     << (allowed_basepair(x.comp[j].pos.second, x.comp[j + 1].pos.first) > 0 ? ") is allowed" : ") is not allowed")
                     << endl;
        }
    }
}

void MOIP::extend_pareto(double lambdaMin, double lambdaMax)
{
    SecondaryStructure s = solve_objective(2, lambdaMin, lambdaMax);
    if (!s.is_empty_structure) {    // A solution has been found
        if (verbose_) cout << "\t>Done, ";
        if (is_undominated_yet(s)) {
            add_solution(s);    // adding the SecondaryStructure s to the set pareto_
            // std::cin.ignore();
            extend_pareto(s.get_objective_score(1), lambdaMax);    // run localPareto above the SecondaryStructure s
        } else {
            if (verbose_) cout << "but solution is dominated." << endl;
        }
    }
}

void MOIP::add_solution(const SecondaryStructure& s)
{
    if (verbose_) cout << '\n';
    vector<size_t> to_remove;
    for (uint i = 0; i < pareto_.size(); i++)
        if (s > pareto_[i]) {    // A solution from the set is now dominated
            // This should only happen in the case some structures have the same optimal Obj1 value.
            if (verbose_) cout << "\t>removing structure from Pareto set : " << pareto_[i].to_string() << endl;
            to_remove.push_back(i);
        }
    if (to_remove.size()) {
        for (size_t i = to_remove.size() - 1; i != 0; i--) pareto_.erase(pareto_.begin() + to_remove[i]);
        pareto_.erase(pareto_.begin() + to_remove[0]);
    }
    if (verbose_) cout << "\t>adding structure to Pareto set :     " << s.to_string() << endl;
    pareto_.push_back(s);
}

size_t MOIP::get_yuv_index(size_t u, size_t v) const
{
    size_t a, b;
    a = (u < v) ? u : v;
    b = (u > v) ? u : v;
    return index_of_yuv_[a][b - 4 - a];
}

size_t MOIP::get_Cpxi_index(size_t x_i, size_t i_on_j) const { return index_of_Cxip_[x_i][i_on_j]; }



bool MOIP::allowed_basepair(size_t u, size_t v) const
{
    size_t a, b;
    a = (v > u) ? u : v;
    b = (v > u) ? v : u;
    if (b - a < 4) return false;
    if (a >= rna_.get_RNA_length() - 6) return false;
    if (b >= rna_.get_RNA_length()) return false;
    if (get_yuv_index(a, b) == rna_.get_RNA_length() * rna_.get_RNA_length() + 1)
        return false;    // not allowed because proba < theta_
    return true;
}
