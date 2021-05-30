#! /bin/bash

WORKDIR=$(pwd)
echo "checking docker image ..."
cd os161_base
if test "$(docker images|grep os161-builder|awk '{print $1}')" = "os161-builder"
then
	echo "docker image found!"
else
	echo "making docker image 'os161-builder'"
	docker build -t os161-builder -f Dockerfile .
fi

echo "=============================="

CONTAINER=$(docker ps|grep os161-builder|awk '{print $1}')

if test "$CONTAINER" = ""
then	
	if [ $"(docker ps -a|grep os161)" ]
       	then 
		echo "restarting container ..."
		docker start -i os161
	else
		echo "running docker ..."
		echo "mounted dir: $WORKDIR"
		docker run -itd --name os161 -v $WORKDIR:/home/os161/cs350-os161 os161-builder
		docker exec -it $(docker ps|grep os161-builder|awk '{print $1}') bash
	fi
else
	echo "existed container found, entering ..."
	docker exec -it $CONTAINER bash
fi
