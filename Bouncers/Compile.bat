del Bouncer.obj
del DecideBouncers.exe
del VerifyBouncers.exe
g++ -std=c++20 -Wall -O3 -c -o Bouncer.obj Bouncer.cpp
g++ -std=c++20 -Wall -O3 -oDecideBouncers DecideBouncers.cpp BouncerDecider.cpp Bouncer.obj ../Params.obj ../Reader.obj ../TuringMachine.obj
g++ -std=c++20 -Wall -O3 -oVerifyBouncers VerifyBouncers.cpp BouncerVerifier.cpp Bouncer.obj ../Params.obj ../Reader.obj ../TuringMachine.obj
