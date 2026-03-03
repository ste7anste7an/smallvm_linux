```
wget https://download.savannah.gnu.org/releases/freetype/freetype-2.13.2.tar.gz  
tar xf freetype-2.13.2.tar.gz  
cd freetype-2.13.2

CFLAGS="-m32" LDFLAGS="-m32" ./configure \  
--disable-shared \  
--enable-static \  
--host=i686-linux-gnu

make -j$(nproc)
```

```
CFLAGS="-m32" LDFLAGS="-m32" ./configure \  
--disable-shared \  
--enable-static \  
--host=i686-linux-gnu

make -j$(nproc)


sudo make install

```

```
wget https://www.libsdl.org/projects/SDL_ttf/release/SDL2_ttf-2.22.0.tar.gz  
tar xf SDL2_ttf-2.22.0.tar.gz  
cd SDL2_ttf-2.22.0

export SDL2_CONFIG=/usr/local/bin/sdl2-config

CFLAGS="-m32 -I/usr/local/include/SDL2 -D_REENTRANT" \
CXXFLAGS="-m32 -I/usr/local/include/SDL2" \
LDFLAGS="-m32 -L/usr/local/lib /usr/local/lib/libSDL2.a -lm -lpthread" \
./configure \
--disable-shared \
--disable-harfbuzz \
--enable-static \
--host=i686-linux-gnu

make -j$(nproc)
```



```
wget https://zlib.net/current/zlib.tar.gz
tar zxvf zlib.tar.gz
cd zlib-1.3.2

CFLAGS="-m32 -fPIC" ./configure --static

make -j$(nproc)  
make install

```

```
wget https://download.sourceforge.net/libpng/libpng-1.6.40.tar.gz  
tar xvf libpng-1.6.40.tar.gz  
cd libpng-1.6.40

CFLAGS="-m32" \
LDFLAGS="-m32 -L/usr/local/libs" \
./configure --disable-shared --enable-static
```
