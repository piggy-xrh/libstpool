How to complie the project ?

WINDOWS:
	stpool_win_proj (vs2008)

LINUX/MAC
   ./configure --prefix=/usr
   make && make install

ARM
  ./configure --prefix=install-dir --host=cross-complier-
  make && make install

NDK(android)
   		./configure --prefix=install-dir --host=cross-complier-
   		make && make install

   Or
    	1. ./configure

		2. Modify the features.mk (Remove some feature MACROs if the NDK does not support them.  eg.-DHAS_PTHREAD_ATTR_GETINHERITSCHED)

		3. ndk-build
  

