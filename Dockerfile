FROM lambci/lambda:build-python2.7

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
RUN git checkout pandafium
RUN gclient sync

RUN build/linux/sysroot_scripts/install-sysroot.py --arch=amd64
RUN gn gen out/Lambda
ADD args.gn out/Lambda

RUN ninja -C out/Lambda
