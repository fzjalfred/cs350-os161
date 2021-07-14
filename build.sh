  
#!/bin/bash

if [ ${#} -ne 1 ]; then
    echo "usage: ./build.sh ASST# " >&2
    exit 1
fi

cd os161-1.99/kern/conf

./conf ${1}

cd ../compile/${1}

bmake depend && bmake && bmake install 
