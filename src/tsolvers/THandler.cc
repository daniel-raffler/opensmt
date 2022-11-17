/*
 *  Copyright (c) 2008-2012 Roberto Bruttomesso
 *  Copyright (c) 2012-2022, Antti Hyvarinen <antti.hyvarinen@gmail.com>
 *  Copyright (c) 2022, Martin Blicha <martin.blicha@gmail.com>
 *
 *  SPDX-License-Identifier: MIT
 */

#include "THandler.h"
#include "TSolver.h"
#include "ModelBuilder.h"

#include <sys/wait.h>
#include <cassert>
#include <sstream>
#include <unordered_set>

void THandler::backtrack(int lev)
{
    unsigned int backTrackPointsCounter = 0;
    // Undoes the state of theory atoms if needed
    while ( (int)stack.size( ) > (lev > 0 ? lev : 0) ) {
        PTRef e = stack.last();
        stack.pop();

        // It was var_True or var_False
        if (e == getLogic().getTerm_true() || e == getLogic().getTerm_false()) continue;

        assert(isDeclared(var(PTRefToLit(e))) == getLogic().isTheoryTerm(e));
        if (not isDeclared(var(PTRefToLit(e)))) continue;
        ++backTrackPointsCounter;
    }
    for (auto solver : getSolverHandler().solverSchedule) {
        solver->popBacktrackPoints(backTrackPointsCounter);
    }

    checked_trail_size = stack.size( );
}

// Push newly found literals from trail to the solvers
bool THandler::assertLits(const vec<Lit> & trail)
{
    bool res = true;

    assert( checked_trail_size == stack.size_( ) );
    assert( (int)stack.size( ) <= trail.size( ) );

#ifdef PEDANTIC_DEBUG
    vec<Lit> assertions;
#endif

    for ( int i = checked_trail_size;
          i < trail.size( ) && (res != false);
          i ++ ) {
        const Lit l = trail[ i ];
        const Var v = var( l );

        PTRef pt_r = tmap.varToPTRef(v);
        stack.push( pt_r );
        assert(isDeclared(v) == getLogic().isTheoryTerm(pt_r));
        if (not isDeclared(v)) continue;
        assert(getLogic().isTheoryTerm(pt_r));


        if ( pt_r == getLogic().getTerm_true() )       { assert(sign(l) == false); continue; }
        else if ( pt_r == getLogic().getTerm_false() ) { assert(sign(l) == true ); continue; }

        res = assertLit(PtAsgn(pt_r, sign(l) ? l_False : l_True));
    }

    checked_trail_size = stack.size( );
    return res;
}


// Check the assignment with equality solver
TRes THandler::check(bool complete) {
    return getSolverHandler().check(complete);
//  if ( complete && config.certification_level > 2 )
//    verifyCallWithExternalTool( res, trail.size( ) - 1 );
}

std::vector<vec<Lit>> THandler::getNewSplits() {
    vec<PTRef> newSplits = getSolverHandler().getSplitClauses();
    std::vector<vec<Lit>> splitClauses;
    if (newSplits.size() == 0) {
        return splitClauses;
    }
    assert((std::unordered_set<PTRef, PTRefHash>{newSplits.begin(), newSplits.end()}.size() == newSplits.size_())); // No duplicates in splits
    for (PTRef clause : newSplits) {
        splitClauses.emplace_back();
        Logic const & logic = getLogic();
        assert(logic.isOr(clause));
        for (int i = 0; i < logic.getPterm(clause).size(); i++) {
            PTRef litTerm = logic.getPterm(clause)[i];
            Lit l = tmap.getOrCreateLit(litTerm);
            PTRef atomTerm = logic.isNot(litTerm) ? logic.getPterm(litTerm)[0] : litTerm;
            assert(getLogic().isAtom(atomTerm)); // MB: Needs to be an atom, otherwise the declaration would not work.
            declareAtom(atomTerm);
            informNewSplit(atomTerm);
            splitClauses.back().push(l);
        }
    }
    return splitClauses;
}

//
// Return the conflict generated by a theory solver
//
void THandler::getConflict (
        vec<Lit> & conflict
        , vec<VarData>& vardata
        , int & max_decision_level
    )
{
    // First of all, the explanation in a tsolver is
    // stored as conjunction of enodes e1,...,en
    // with associated polarities p1,...,pn. Since the sat-solver
    // wants a clause we store it in the form ( l1 | ... | ln )
    // where li is the literal corresponding with ei with polarity !pi
    vec<PtAsgn> explanation;
    {
        bool found = false;
        for (auto solver : getSolverHandler().solverSchedule) {
            if (solver->hasExplanation()) {
                solver->getConflict(explanation);
                found = true;
                break;
            }
        }
        (void)found;
        assert(found);
    }

    if (explanation.size() == 0) {
        max_decision_level = 0;
        return;
    }

    max_decision_level = -1;
    for (int i = 0; i < explanation.size(); ++i) {
        PtAsgn const & ei = explanation[i];
        assert(ei.sgn == l_True || ei.sgn == l_False);
        Var v = ptrefToVar(ei.tr);
        assert(v != var_Undef);
        bool negate = ei.sgn == l_False;
        Lit l = mkLit(v, !negate);
        conflict.push(l);

        if (max_decision_level < vardata[v].level) {
            max_decision_level = vardata[v].level;
        }
    }
}


PTRef
THandler::getInterpolant(const ipartitions_t& mask, ItpColorMap * labels, PartitionManager &pmanager)
{
    return getSolverHandler().getInterpolant(mask, labels, pmanager);
}

//
// It is in principle possible that the egraph contains deduceable literals
// that the SAT solver is not aware of because they have been simplified due to
// appearing only in clauses that are tautological.  We check this here, but it
// would be better to remove them from egraph after simplifications are done.
//
Lit THandler::getDeduction() {
    PtAsgn_reason e = PtAsgn_reason_Undef;
    while (true) {
        for (auto solver : getSolverHandler().solverSchedule) {
            e = solver->getDeduction();
            if (e.tr != PTRef_Undef) break;
        }
        if ( e.tr == PTRef_Undef ) {
            return lit_Undef;
        }
        //assert(e.reason != PTRef_Undef);
        //assert(e.sgn != l_Undef);
#ifdef PEDANTIC_DEBUG
        if (!tmap.hasLit(e.tr))
            cerr << "Missing (optimized) deduced literal ignored: " << getLogic().printTerm(e.tr) << '\n';
#endif
        if (!tmap.hasLit(e.tr)) continue;
        break;
    }
    return e.sgn == l_True ? tmap.getLit(e.tr) : ~tmap.getLit(e.tr);
}

Lit THandler::getSuggestion( ) {
    PTRef e = PTRef_Undef; // egraph.getSuggestion( );

    if ( e == PTRef_Undef )
        return lit_Undef;

//  bool negate = e->getDecPolarity( ) == l_False;
//  Var v = enodeToVar( e );
//  return Lit( v, negate );
    
    return tmap.getLit(e);
}

void THandler::getReason( Lit l, vec< Lit > & reason)
{
    Var   v = var(l);
    PTRef e = tmap.varToPTRef(v);

    // It must be a TAtom and already deduced
    assert(getLogic().isTheoryTerm(e));
    TSolver* solver = getSolverHandler().getReasoningSolverFor(e);
    assert(solver);

    // Get Explanation
    vec<PtAsgn> explanation = solver->getReasonFor(PtAsgn(e, sign(l) ? l_False : l_True));
    assert(explanation.size() > 0);

    // Reserve room for implied lit
    reason.push( lit_Undef );
    // Copy explanation

    while ( explanation.size() > 0 ) {
        PtAsgn pa = explanation.last();
        PTRef ei  = pa.tr;
        explanation.pop();

        // Toggle polarity for deduced literal
        if ( e == ei ) {
            // The deduced literal must have been pushed
            // with the the same polarity that has been deduced
            assert((pa.sgn == l_True && sign(l)) || (pa.sgn == l_False && !sign(l))); // The literal is true (sign false) iff the egraph term polarity is false
            reason[0] = l;
        }
        else {
            assert(pa.sgn != l_Undef);
            reason.push(pa.sgn == l_True ? ~tmap.getLit(ei) : tmap.getLit(ei)); // Swap the sign for others
        }
    }

}

#ifdef PEDANTIC_DEBUG

bool THandler::isOnTrail( Lit l, vec<Lit>& trail ) {
    for ( int i = 0 ; i < trail.size( ) ; i ++ )
        if ( trail[ i ] == l ) return true;

    return false;
}

#endif


void
THandler::dumpFormulaToFile( std::ostream & dump_out, PTRef formula, bool negate )
{
	std::vector< PTRef > unprocessed_enodes;
	std::map< PTRef, std::string, std::greater<PTRef> > enode_to_def;
	unsigned num_lets = 0;
    Logic& logic = getLogic();

	unprocessed_enodes.push_back( formula );
	// Open assert
	dump_out << "(assert" << '\n';
	//
	// Visit the DAG of the formula from the leaves to the root
	//
	while( !unprocessed_enodes.empty( ) )
	{
		PTRef e = unprocessed_enodes.back( );
		//
		// Skip if the node has already been processed before
		//
		if ( enode_to_def.find( e ) != enode_to_def.end( ) )
		{
			unprocessed_enodes.pop_back( );
			continue;
		}

		bool unprocessed_children = false;
        Pterm& term = logic.getPterm(e);
        for(int i = 0; i < term.size(); ++i)
        {
            PTRef pref = term[i];
            //assert(isTerm(pref));
			//
			// Push only if it is unprocessed
			//
			if ( enode_to_def.find( pref ) == enode_to_def.end( ) && (logic.isBooleanOperator( pref ) || logic.isEquality(pref)))
			{
				unprocessed_enodes.push_back( pref );
				unprocessed_children = true;
			}
		}
		//
		// SKip if unprocessed_children
		//
		if ( unprocessed_children ) continue;

		unprocessed_enodes.pop_back( );

		char buf[ 32 ];
		sprintf( buf, "?def%d", Idx(logic.getPterm(e).getId()) );

		// Open let
		dump_out << "(let ";
		// Open binding
		dump_out << "((" << buf << " ";

		if (term.size() > 0 ) dump_out << "(";
		dump_out << logic.printSym(term.symb());
        for(int i = 0; i < term.size(); ++i)
		{
            PTRef pref = term[i];
			if ( logic.isBooleanOperator(pref) || logic.isEquality(pref) )
				dump_out << " " << enode_to_def[ pref ];
			else
			{
				dump_out << " " << logic.printTerm(pref);
				if ( logic.isAnd(e) ) dump_out << '\n';
			}
		}
		if ( term.size() > 0 ) dump_out << ")";

		// Closes binding
		dump_out << "))" << '\n';
		// Keep track of number of lets to close
		num_lets++;

		assert( enode_to_def.find( e ) == enode_to_def.end( ) );
		enode_to_def[ e ] = buf;
	}
	dump_out << '\n';
	// Formula
	if ( negate ) dump_out << "(not ";
	dump_out << enode_to_def[ formula ] << '\n';
	if ( negate ) dump_out << ")";
	// Close all lets
	for( unsigned n=1; n <= num_lets; n++ ) dump_out << ")";
	// Closes assert
	dump_out << ")" << '\n';
}

void
THandler::dumpHeaderToFile(std::ostream& dump_out)
{
    Logic& logic = getLogic();
    dump_out << "(set-logic QF_UF)" << '\n';

    /*
	dump_out << "(set-info :source |" << '\n'
			<< "Dumped with "
			<< PACKAGE_STRING
			<< " on "
			<< __DATE__ << "." << '\n'
			<< "|)"
			<< '\n';
	dump_out << "(set-info :smt-lib-version 2.0)" << '\n';
    */

    logic.dumpHeaderToFile(dump_out);
}

char* THandler::printAsrtClause(vec<Lit>& r) {
    std::stringstream os;
    for (int i = 0; i < r.size(); i++) {
        Var v = var(r[i]);
        bool sgn = sign(r[i]);
        os << (sgn ? "not " : "") << getLogic().printTerm(tmap.varToPTRef(v)) << " ";
    }
    return strdup(os.str().c_str());
}

char* THandler::printAsrtClause(Clause* c) {
    vec<Lit> v;
    for (unsigned i = 0; i < c->size(); i++)
        v.push((*c)[i]);
    return printAsrtClause(v);
}

bool THandler::checkTrailConsistency(vec<Lit>& trail) {
    (void)trail;
    assert(trail.size() >= stack.size()); // There might be extra stuff
                                          // because of conflicting assignments
    for (int i = 0; i < stack.size(); i++) {
        assert(var(trail[i]) == var(tmap.getLit(stack[i])));
//        ||
//               (stack[i] == logic.getTerm_false() &&
//                trail[i] == ~tmap.getLit(stack[i])));
    }
    return true;
}

#ifdef PEDANTIC_DEBUG
std::string THandler::printAssertion(Lit assertion) {
    stringstream os;
    os << "; assertions ";
    Var v = var(assertion);
    PTRef pt_r = tmap.varToPTRef(v);
    if (sign(assertion))
        os << "!";
    os << getLogic().term_store.printTerm(pt_r, true) << "[var " << v << "] " << '\n';
    return os.str();
}

//std::string THandler::printExplanation(vec<PtAsgn>& explanation, vec<char>& assigns) {
//    stringstream os;
//    os << "; Conflict: ";
//    for ( int i = 0 ; i < explanation.size( ) ; i ++ ) {
//        if ( i > 0 )
//            os << ", ";
//        Var v = tmap.getVar(explanation[i].tr);
//        lbool val = toLbool(assigns[v]);
//        assert(val != l_Undef);
//        if ( val == l_False )
//            os << "!";
//
//        os << getLogic().term_store.printTerm(explanation[i].tr);
//        os << "[var " << v << "]";
//    }
//    os << '\n';
//    return os.str();
//}
#endif

void THandler::clear() { declared.clear(); getSolverHandler().clearSolver(); }  // Clear the solvers from their states

Theory& THandler::getTheory() { return theory; }
Logic&  THandler::getLogic()  { return theory.getLogic(); }

TSolverHandler&       THandler::getSolverHandler()       { return theory.getTSolverHandler(); }
const TSolverHandler& THandler::getSolverHandler() const { return theory.getTSolverHandler(); }
TermMapper&           THandler::getTMap()                { return tmap; }

void    THandler::fillTheoryFunctions(ModelBuilder &modelBuilder) const { getSolverHandler().fillTheoryFunctions(modelBuilder); }

PTRef   THandler::varToTerm          ( Var v ) const { return tmap.varToPTRef(v); }  // Return the term ref corresponding to a variable
Pterm&  THandler::varToPterm         ( Var v)        { return getLogic().getPterm(tmap.varToPTRef(v)); } // Return the term corresponding to a variable
Lit     THandler::PTRefToLit         ( PTRef tr)     { return tmap.getLit(tr); }

std::string THandler::getVarName(Var v) const { return getLogic().printTerm(tmap.varToPTRef(v)); }

Var     THandler::ptrefToVar         ( PTRef r ) { return tmap.getVar(r); }

void    THandler::computeModel      () { getSolverHandler().computeModel(); } // Computes a model in the solver if necessary
void    THandler::clearModel        () { /*getSolverHandler().clearModel();*/ }   // Clear the model if necessary

bool    THandler::assertLit         (PtAsgn pta) { return getSolverHandler().assertLit(pta); } // Push the assignment to all theory solvers
void    THandler::informNewSplit    (PTRef tr) { getSolverHandler().informNewSplit(tr);  } // The splitting variable might need data structure changes in the solver (e.g. LIA needs to re-build bounds)

void THandler::declareAtom(PTRef tr) {
    Var v = ptrefToVar(tr);
    declared.growTo(v + 1, false);
    declared[v] = true;
    getSolverHandler().declareAtom(tr);
}

inline double THandler::drand(double& seed)
{
    seed *= 1389796;
    int q = (int)(seed / 2147483647);
    seed -= (double)q * 2147483647;
    return seed / 2147483647;
}

// Returns a random integer 0 <= x < size. Seed must never be 0.
inline int THandler::irand(double& seed, int size)
{
    return (int)(drand(seed) * size);
}

inline lbool THandler::value (Lit p, vec<lbool>& assigns) const { return assigns[var(p)] ^ sign(p); }