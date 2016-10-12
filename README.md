flloc: A dynamic memory debugger
================================


Introduction
------------

flloc is dual-licensed under the GPLv3 and a commercial license. If you
require the commercial license, please contact me: "Fabrice Triboix"
<ftriboix-at-gmail-dot-com>.

This project has been created because I couldn't find a simple,
thread-aware memory debugger that would minimise its impact on the
running of the software.


Getting started
---------------

Follow the steps:

    $ $EDITOR Makefile
    $ make
    $ ./run-tests.py    # Run the unit tests
    $ make install      # Install


How to use flloc
----------------

In your source files, include `flloc.h`. Add `-lflloc` to the list of
libraries you link against. Then run your executable. Flloc will print
memory corruptions in the guard buffers (see below), and any detected
memory leak when the executable exits.

You can tune flloc behaviour by setting the `FLLOC_CONFIG` environment
variable prior to running your executable thus:

    $ export FLLOC_CONFIG="FILE=/path/to/flloc.log;GUARD=10000"

Where `FILE` is the path to a file where flloc will write its output
(default is to print to stderr), and `GUARD` is the size of the guard
buffers (in bytes). Guard buffers are padding before and after each
dynamically allocated block of memory; they are used to detect
corruptions, where the code writes into memory outside what has been
allocated. You can set it to 0 to disable this feature (default is
1024).


No warranties
-------------

I wrote these pieces of code on my spare time in the hope that they will
be useful. I make no warranty at all on their suitability for use in
any type of software application.


Copyright
---------

This software is dual-license under the GPLv3 and a commercial license.
The text of the GPLv3 is available in the [LICENSE](LICENSE) file.

A commercial license can be provided if you do not wish to be bound by
the terms of the GPLv3, or for other reasons. Please contact me for more
details.

