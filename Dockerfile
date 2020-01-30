FROM lambci/lambda:build-python2.7 as build-environment

RUN yum install -y git-all libjpeg-turbo-devel libjpeg-turbo-static freetype-devel

WORKDIR /opt
RUN mkdir depot_tools
RUN chown ec2-user depot_tools
RUN git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
RUN echo 'export PATH=/opt/depot_tools:"$PATH"' >> ~/.bashrc
ENV PATH="/opt/depot_tools:${PATH}"

RUN mkdir /root/repo
WORKDIR /root/repo
RUN gclient config --unmanaged https://github.com/gradescope/pdfium.git && gclient sync
WORKDIR /root/repo/pdfium

ARG revision=pandafium
RUN git fetch && git checkout $revision
RUN gclient sync

RUN build/linux/sysroot_scripts/install-sysroot.py --arch=amd64
RUN gn gen out/Lambda
ADD args.gn out/Lambda
RUN gn gen out/Lambda

RUN ninja -C out/Lambda samples:pandafium

# Multistage build
FROM lambci/lambda:build-python2.7
COPY --from=build-environment /root/repo/pdfium/out/Lambda/pandafium /usr/local/bin/pandafium
CMD pandafium
