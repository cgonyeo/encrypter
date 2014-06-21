#lang racket
(require openssl)

(define port (tcp-listen 3254 4 #f "localhost"))

(let loop ()
  (let-values (((cin cout) (tcp-accept port))
               ((sin sout) (ssl-connect "skynet.csh.rit.edu" 6697)))
    (printf "Connection made\n")
    (thread (lambda () (copy-port sin cout)))
    (thread (lambda () (copy-port cin sout)))
    (loop)))
