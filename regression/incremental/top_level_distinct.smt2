(set-logic QF_UF)
(declare-sort U 0)
(declare-fun x0 () U)
(declare-fun x1 () U)
(declare-fun x2 () U)

(push 1)
(assert (distinct x0 x1 x2))
(check-sat)
(pop 1)
(push 1)
(assert (= x0 x1))
(check-sat)
(exit)
