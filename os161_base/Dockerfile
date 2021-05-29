FROM debian:jessie

# First install GCC 4.9, make, and ncurses (required for GDB)
RUN apt-get update && apt-get install -y make gcc-4.9 libncurses5-dev
RUN ln -s /usr/bin/gcc-4.9 /usr/bin/gcc

RUN useradd -ms /bin/bash os161
ENV HOME=/home/os161
RUN mkdir -p $HOME

# Expect these archives to be in the provided directory
# Docker will extract them automatically into /home
ADD os161-binutils.tar.gz $HOME/
ADD os161-gcc.tar.gz $HOME/
ADD os161-gdb.tar.gz $HOME/
ADD os161-bmake.tar.gz $HOME/
ADD os161-mk.tar.gz $HOME/
ADD sys161.tar.gz $HOME/

# The USER directive only applies to RUN, CMD, and ENTRYPOINT, but not ADD
# So we have to ADD the archives as root and then chown them to the user
RUN chown -R os161 ${HOME}
USER os161

# Build and Install binutils
WORKDIR $HOME/binutils-2.17+os161-2.0.1
RUN ./configure -nfp --disable-werror --target=mips-harvard-os161 --prefix=$HOME/sys161/tools
RUN make
RUN make install

# Update the PATH
ENV TOOLCHAIN="$HOME/sys161/bin:$HOME/sys161/tools/bin"
ENV PATH="$TOOLCHAIN:$PATH"

# Install the cross compiler
WORKDIR $HOME/gcc-4.1.2+os161-2.0
RUN ./configure -nfp --disable-shared --disable-threads --disable-libmudflap --disable-libssp --target=mips-harvard-os161 --prefix=$HOME/sys161/tools
RUN make
RUN make install

# Install GDB
WORKDIR $HOME/gdb-6.6+os161-2.0
RUN ./configure --disable-werror --target=mips-harvard-os161 --prefix=$HOME/sys161/tools
RUN make
RUN make install

# Install bmake
WORKDIR $HOME/bmake
RUN mkdir mk
RUN mv $HOME/mk/* mk/
RUN rm -r $HOME/mk
RUN ./boot-strap --prefix=$HOME/sys161/tools

# These are the commands that ./boot-strap outputs under the $HOME/sys161/tools install instructions
RUN mkdir -p $HOME/sys161/tools/bin
RUN cp $HOME/bmake/Linux/bmake $HOME/sys161/tools/bin/bmake-20101215
RUN rm -f $HOME/sys161/tools/bin/bmake
RUN ln -s bmake-20101215 $HOME/sys161/tools/bin/bmake
RUN mkdir -p $HOME/sys161/tools/share/man/cat1
RUN cp $HOME/bmake/bmake.cat1 $HOME/sys161/tools/share/man/cat1/bmake.1
RUN sh $HOME/bmake/mk/install-mk $HOME/sys161/tools/share/mk

# Set up links to toolchain binaries
RUN mkdir -p $HOME/sys161/bin
WORKDIR $HOME/sys161/tools/bin
RUN sh -c 'for i in mips-*; do ln -s $HOME/sys161/tools/bin/$i $HOME/sys161/bin/cs350-`echo $i | cut -d- -f4-`; done'
RUN ln -s $HOME/sys161/tools/bin/bmake $HOME/sys161/bin/bmake

# Build and install sys161
WORKDIR $HOME/sys161-1.99.06
RUN ./configure --prefix=$HOME/sys161 mipseb
RUN make
RUN make install

# Link to the example conf file
WORKDIR $HOME/sys161
RUN ln -s share/examples/sys161/sys161.conf.sample sys161.conf

# Remove the sources that are no longer needed
RUN rm -R $HOME/binutils-2.17+os161-2.0.1
RUN rm -R $HOME/gcc-4.1.2+os161-2.0
RUN rm -R $HOME/gdb-6.6+os161-2.0
RUN rm -R $HOME/bmake
RUN rm -R $HOME/sys161-1.99.06

# Create a working directory for the OS161 source code
# This image doesn't contain the source code, so it should be bind mounted to a directory on the host that does
ENV SRCDIR="$HOME/cs350-os161"
RUN mkdir -p $SRCDIR
WORKDIR $SRCDIR