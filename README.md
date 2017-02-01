# ai-statefultask submodule

This repository is a [git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules)
providing C++ utilities for larger projects, including:

* Stuff here

The root project should be using
[autotools](https://en.wikipedia.org/wiki/GNU_Build_System autotools),
[cwm4](https://github.com/CarloWood/cwm4) and
[libcwd](https://github.com/CarloWood/libcwd).

## Example

## Checking out a project that uses the ai-statefultask submodule.

To clone a project example-project that uses ai-statefultask simply run:

<pre>
<b>git clone --recursive</b> &lt;<i>URL-to-project</i>&gt;<b>/example-project.git</b>
<b>cd example-project</b>
<b>./autogen.sh</b>
</pre>

The <tt>--recursive</tt> is optional because <tt>./autogen.sh</tt> will fix
it when you forgot it.

Afterwards you probably want to use <tt>--enable-mainainer-mode</tt>
as option to the generated <tt>configure</tt> script.

## Adding the ai-statefultask submodule to a project

To add this submodule to a project, that project should already
be set up to use [cwm4](https://github.com/CarloWood/cwm4).

Simply execute the following in a directory of that project
where you what to have the <tt>statefultask</tt> subdirectory:

<pre>
git submodule add https://github.com/CarloWood/ai-statefultask.git statefultask
</pre>

This should clone ai-statefultask into the subdirectory <tt>statefultask</tt>, or
if you already cloned it there, it should add it.

Changes to <tt>configure.ac</tt> and <tt>Makefile.am</tt>
are taken care of my <tt>cwm4</tt>, except for linking
which works as usual.

For example a module that defines a

<pre>
bin_PROGRAMS = foobar
</pre>

would also define

<pre>
foobar_CXXFLAGS = @LIBCWD_R_FLAGS@
foobar_LDADD = ../statefultask/statefultask.la ../threadsafe/threadsafe.la ../utils/libutils_r.la ../cwd/libcwd_r.la @LIBCWD_R_LIBS@
</pre>

or whatever the path to `statefultask` etc. is, to link with the required submodules,
libraries, and assuming you'd also use the [cwd](https://github.com/CarloWood/cwd) submodule.

Finally, run

<pre>
./autogen.sh
</pre>

to let cwm4 do its magic, and commit all the changes.

Checkout [ai-statefultask-testsuite](https://github.com/CarloWood/ai-statefultask-testsuite)
for an example of a project that uses this submodule.
