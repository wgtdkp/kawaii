;(define (fib n)
;  (if (= 0 n)
;      0
;      (if (= 1 n)
;          1
;          (+ (fib (- n 1)) (fib (- n 2))))))

(define (fib n)
  (define (fib-iter n a b)
    (if (= n 0) 0
      (if (= n 1) b (fib-iter (- n 1) b (+ a b)))))
  (fib-iter n 0 1))

(fib 40)
(not (= 0 0))
;(fib 5)
;(fib 6)
;(fib 20)
;(fib 30)
