MEMKIND_DIR=../../memkind

export LD_LIBRARY_PATH=$(cd ..; pwd)/lib:$(cd $MEMKIND_DIR; pwd)/.libs