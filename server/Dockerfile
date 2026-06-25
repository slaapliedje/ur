# Royal Game of Ur — game server image (build context = this server/ dir):
#   docker build -t ur-server server/
#   docker run -d --name ur -p 1234:1234 -p 8080:8080 -v ur-data:/data ur-server
FROM golang:1.22-alpine AS build
WORKDIR /src
COPY . .
RUN CGO_ENABLED=0 go build -trimpath -o /ur-server .

FROM alpine:3.20
RUN adduser -D -u 10001 ur
COPY --from=build /ur-server /usr/local/bin/ur-server
USER ur
WORKDIR /data
# TCP game port (clients connect via N:TCP) + HTTP leaderboard. Stats persist
# under /data — mount a volume there to keep them across container restarts.
ENV UR_ADDR=":1234" UR_HTTP_ADDR=":8080" UR_DATA="/data/ur-stats.json"
EXPOSE 1234 8080
ENTRYPOINT ["ur-server"]
