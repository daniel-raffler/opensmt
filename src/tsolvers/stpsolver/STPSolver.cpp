//
// Created by Martin Blicha on 12.12.19.
//

#include "STPSolver.h"

static SolverDescr descr_stp_solver("STP Solver", "Solver for Simple Temporal Problem (Difference Logic)");

STPSolver::STPSolver(SMTConfig & c, LALogic & l, vec<DedElem> & d)
        : TSolver((SolverId)descr_stp_solver, (const char*)descr_stp_solver, c, d)
        , logic(l)
{

}

STPSolver::~STPSolver() = default;

// TODO: implement checks on correctness of format
Edge STPSolver::parseEdge(PTRef e) {
    // Atoms made by mkNumLeq are in form "-c <= -x + y", so we need to negate the coefficients

    // e[0] is the constant, e[1] is the difference
    opensmt::Number c = logic.getNumConst(logic.getPterm(e)[0]) * -1;
    PTRef add = logic.getPterm(e)[1];                   // the subtraction is stored as an addition with one term negated

    PTRef y = logic.getPterm(add)[0];                   // first term is 'y'
    PTRef mul = logic.getPterm(add)[1];                 // second term is '-1 * x'
    if (!logic.isNumVar(y)) std::swap(y, mul);  // or they might be reversed
    PTRef x = logic.getPterm(mul)[1];                   // assumes multiplication constant is -1

    return Edge{.from = x, .to = y, .cost = c};
}

void STPSolver::declareAtom(PTRef tr) {
    // This method is used from outside to tell the solver about a possible atom that can later be asserted positively
    // or negatively
    // Ignore everything else other than atoms of the form "x - y <= c"; i.e., variable minus variable is less or equal
    // to some constant
    // TODO: store information about the term tr if necessary
    Edge e = parseEdge(tr);
    graph.addVertex(e.from);
    graph.addVertex(e.to);
}

bool STPSolver::assertLit(PtAsgn asgn, bool b) {
    // Actually asserting an atom to the solver - adding a new constraint to the current set
    // asgn.tr is the atom to add
    // asgn.sgn is the polarity (positive or negative)
    // TODO: process the addition of the constraint to the current set of constraints
    //      Return false if immediate conflict has been detected, otherwise return true
    //      Postpone actual checking of consistency of the extended set of constraints until call to the "check" method

    Edge e = parseEdge(asgn.tr);
    if (asgn.sgn == l_False) {             // negating the edge if needed
       auto tmp = e.from;
       e.from = e.to;
       e.to = tmp;
       e.cost = -(e.cost + 1);   // Works only on integer values!!!
    }

    return graph.addEdge(e);     // performs consistency check. Must be refactored
}

TRes STPSolver::check(bool b) {
    // The main method checking the consistency of the current set of constraints
    // Return SAT if the current set of constraints is satisfiable, UNSAT if unsatisfiable
    // TODO: implement the main check of consistency
    return graph.check() ? TRes::SAT : TRes::UNSAT;
}

void STPSolver::clearSolver() {
    TSolver::clearSolver();
}

void STPSolver::print(ostream & out) {

}

void STPSolver::pushBacktrackPoint() {
    // Marks a checkpoint for the set of constraints in the solver
    // Important for backtracking
    TSolver::pushBacktrackPoint();
}

void STPSolver::popBacktrackPoint() {
    // This is just for compatibility with older architecture
    // Typically multiple backtrack points are popped together, hence popBacktackPoints is the important method to implement
    popBacktrackPoints(1);
}

void STPSolver::popBacktrackPoints(unsigned int i) {
    // This method is called after unsatisfiable state is detected
    // The solver should remove all constraints that were pushed to the solver in the last "i" backtrackpoints
    TSolver::popBacktrackPoints(i);
}

ValPair STPSolver::getValue(PTRef pt) {
    // In the current model, get the value (represented as string) of the term "pt".
    return ValPair();
}

void STPSolver::computeModel() {
    // In case of satisfiability prepare a model witnessing the satisfiability of the current set of constraints

}

void STPSolver::getConflict(bool b, vec<PtAsgn> & vec) {
    // In case of unsatisfiability, return the witnessing subset of constraints
    // The bool parameter can be ignored, the second parameter is the output parameter

}

PtAsgn_reason STPSolver::getDeduction() {
    return PtAsgn_reason();
}


Logic & STPSolver::getLogic() {
    return logic;
}

bool STPSolver::isValid(PTRef tr) {
    return false;
}
