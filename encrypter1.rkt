#lang racket
(require openssl)

(define port (tcp-listen 3254 4 #f "localhost"))

(define (copy-stream-avail in out)
  (let ((bytes (make-bytes 1024)))
    ))

(let loop ((hash (make-immutable-hash))
           (ports (list port)))
  (let ((port1 (apply sync ports)))
    (cond ((tcp-listener? port1)
           (let-values (((cin cout) (tcp-accept port1))
                        ((sin sout) (ssl-connect "skynet.csh.rit.edu" 6697)))
             (loop (hash-set* hash cin sout sin cout)
                   (list* cin sin ports)
                   buf)))
          (t ))))
