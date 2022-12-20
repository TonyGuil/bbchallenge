del Bouncer.obj
del DecideBouncers.exe
del VerifyBouncers.exe
g++ -Wall -O3 -I"C:/Program Files (x86)/boost_1_72_0" -c -o Bouncer.obj Bouncer.cpp
g++ -Wall -O3 -I"C:/Program Files (x86)/boost_1_72_0" -oDecideBouncers DecideBouncers.cpp BouncerDecider.cpp Bouncer.obj ../boost_thread.a
g++ -Wall -O3 -I"C:/Program Files (x86)/boost_1_72_0" -oVerifyBouncers VerifyBouncers.cpp BouncerVerifier.cpp Bouncer.obj
