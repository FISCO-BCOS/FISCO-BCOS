#!/bin/bash

data_dir="~/app/tars/"


genOne() {
  local id=${1}
  cat <<EOF >"docker-compose${id}.yml"

version: "3"
networks:
  tars_net:
    external:
      name: tars-network

services:
  tars-node${id}:
    image: tarscloud/tars-node:latest
    networks:
      tars_net:
        ipv4_address: 172.25.0.${id}
    ports:
      - "10200-10205:10200-10205"
      - "40300-40305:40300-40305"
      - "9000-9010:9000-9010"
    environment:
      INET: eth0
      WEB_HOST: "http://172.25.0.3:3000"
    restart: always
    volumes:
      - ${data_dir}/node${id}:/data/tars
      - /etc/localtime:/etc/localtime
EOF
}

for id in {5..8} ; do
  genOne ${id}
done
