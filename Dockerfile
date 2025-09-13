FROM gcc AS build

ADD src /src

WORKDIR /

RUN gcc -Wno-unused-result -O3 -o main /src/main.c /src/madv_cache.c


FROM busybox

COPY --from=build /main /main

ENTRYPOINT ["./main"]
