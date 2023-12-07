#Writing RocksDB applications with the ZenFS plugin

The ZenFS plugin can also be used in your own RocksDB applications.
To help you writing your own applications you can refer to the examples in this
directory.

To compile an example application you first need to build RocksDB with the
ZenFS plugin acording to
[this section](https://github.com/westerndigitalcorporation/zenfs#build).

Then you can run:
```
g++ -I../../.. -I../../../include  $(pkg-config --cflags rocksdb) -c ./rw_key.cc -o ./rw_key.o
g++ -L../../.. $(pkg-config --static --libs rocksdb) ./rw_key.o -o ./rw_key
./rw_key nvmeXnX
```
