(set-info :smt-lib-version 2.6)
(set-logic QF_UFIDL)
(set-info :source |Benchmarks from the paper: "Extending Sledgehammer with SMT Solvers" by Jasmin Blanchette, Sascha Bohme, and Lawrence C. Paulson, CADE 2011.  Translated to SMT2 by Andrew Reynolds and Morgan Deters.|)
(set-info :category "industrial")
(set-info :status unsat)
(declare-sort S1 0)
(declare-fun f1 () S1)
(declare-fun f2 () S1)
(declare-fun f3 () Int)
(assert (not (= f1 f2)))
(assert (not (=> (not (<= 0 f3)) (< f3 0))))
(check-sat)
(exit)
