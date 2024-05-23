set -e
set -x

X86_PACKAGE=1

######### parsing arguments #############
POSITIONAL=()
while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    --x86)
    X86_PACKAGE=1
    shift # past argument
    ;;
    --aarch64)
    X86_PACKAGE=""
    shift # past argument
    ;;
    *)    # unknown option    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters
##########################################

rm -rf include lib
rm -rf pbc.tar.gz
if [ ! -z "$X86_PACKAGE" ]; then
    bash build.sh -r
else
    # use gcc compiler for aarch64
    bash build.sh -r --gcc
fi

mkdir -p include/pbc/ lib
cp src/compress-c.h include/pbc
cp -f build/src/libpbc.a build/src/deps/fse/libpbc_fse.a build/src/deps/fsst/libpbc_fsst.a lib
cp -f third-party/install/lib/libzstd.a third-party/install/lib64/libhs.a lib
tar -cvf pbc.tar.gz include/pbc lib
md5sum pbc.tar.gz
rm -rf include lib

set +x
set +e
