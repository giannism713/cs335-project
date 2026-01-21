# cs335-project
CS335 Project


-> Run bandwith test without our own implementation: 
- server: ./bandwidth_test -s -p 9090 -f out.txt
- client: ./bandwidth_test -p 9090 -a 127.0.0.1 -f test.txt

-> Run bandwidth MICROTCP: 
- server: ./bandwidth_test -s -m -p 9090 -f out.txt
- client: ./bandwidth_test -m -p 9090 -a 127.0.0.1 -f test.txt