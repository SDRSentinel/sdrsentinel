ARG BASE_IMAGE=ubuntu:noble
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

WORKDIR /root
COPY do_build.sh /root/do_build.sh
RUN chmod +x /root/do_build.sh

ENTRYPOINT ["/root/do_build.sh"]
