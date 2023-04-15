del FAR_Verifier.obj
del DecideFAR.exe
del VerifyFAR.exe
g++ -std=c++20 -Wall -O3 -c -o FAR_Verifier.obj FAR_Verifier.cpp
g++ -std=c++20 -Wall -O3 -oDecideFAR DecideFAR.cpp FAR_Decider.cpp FAR_Verifier.obj ../Params.obj ../Reader.obj ../TuringMachine.obj
g++ -std=c++20 -Wall -O3 -oVerifyFAR VerifyFAR.cpp FAR_Verifier.obj ../Params.obj ../Reader.obj ../TuringMachine.obj