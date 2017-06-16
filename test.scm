;(+ (+ 4 5) (- 3 4) (* 1 2 3 4))
;(define add (+ 3 4))
;add
(define (add) (lambda (a b) (+ a b) (- a b)))
((add) 3 4)
;(+ (- 4 6) 23)
;(((add + -) 4 6)  23)
;()
;(3)
