FROM alpine:3.20 AS builder

RUN apk add --no-cache build-base libsodium-dev sqlite-dev

WORKDIR /src
COPY common/ common/
COPY server/  server/

RUN mkdir build && make -C server BUILD=../build


FROM alpine:3.20

RUN apk add --no-cache libsodium sqlite-libs

COPY --from=builder /src/build/diary-server /usr/local/bin/diary-server

VOLUME ["/data"]
EXPOSE 4242

ENTRYPOINT ["diary-server", "-p", "4242", "-k", "/data/auth.pub", "-db", "/data/diary.db"]
