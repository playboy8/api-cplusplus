#g++ main.cpp -std=c++11 -DLINUX -D_GLIBCXX_USE_CXX11_ABI=0 -DLOGGING_LEVEL_2 -O2 -I../include -I/opt/ssl102/include -L/opt/ssl102/lib  -lDolphinDBAPI -lpthread -lssl  -lcrypto -L../bin/linux_x64/ABI0  -Wl,-rpath,.:../bin/linux_x64/ABI0 -o main

g++ main.cpp ../opt/public.cpp -std=c++11 -DLINUX -D_GLIBCXX_USE_CXX11_ABI=1 -DLOGGING_LEVEL_2 -O2 -I../opt -I../include  -I/opt/ssl102/include -L/opt/ssl102/lib  -lDolphinDBAPI -lpthread -lssl  -lcrypto -luuid -L../bin/linux_x64/ABI1  -Wl,-rpath,.:../bin/linux_x64/ABI1 -o main
