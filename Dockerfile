FROM debian

RUN apt-get update && apt-get install -y clang build-essential gdb

ADD . /app

WORKDIR /app

RUN make build/test
