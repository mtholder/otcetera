# otcetera - phylogenetic file format parser in C++ 
[![Build Status](https://secure.travis-ci.org/OpenTreeOfLife/otcetera.png)](http://travis-ci.org/OpenTreeOfLife/otcetera)

otcetera owes a lot of code and ideas to Paul Lewis' Nexus Class Library.
  See http://hydrodictyon.eeb.uconn.edu/ncl/ and 
  https://github.com/mtholder/ncl

It also uses easyloggingpp which is distributed under an MIT License. See
  http://github.com/easylogging/ for info on that project. The file from
  that project is otc/easylogging++.h



## Installation

### prerequisites
To facilitate parsing of NexSON, this version of requires rapidjson.
Download it from https://github.com/miloyip/rapidjson
and put the path to its include subdir in a "-I" CPPFLAGS or CXXFLAGS
when you run configure.

You also need the whole autotools stack including libtool.

### configuration + building

To run the whole autoreconf stuff in a manner that will add missing bits as needed,
run:

    $ sh bootstrap.sh

Then to configure and build with clang use:

    $ mkdir buildclang
    $ cd buildclang
    $ bash ../reconf-clang.sh
    $ make
    $ make check
    $ make install
    $ make installcheck

To use g++, substitute `reconf-gcc.sh` for `reconf-clang.sh` in that work flow.

Python 2 (recent enough to have the subprocess module as part of the standard lib)
is required for the `make check` operation to succeed.


## Usage
### Common command line flags
The tools use the same (OTCLI) class to process command line arguments. 
This provides the following command line flags:
  * `-h` for help
  * `-v` for verbose output
  * `-q` for quieter than normal output
  * `-t` for trace level (extremely verbose) output

Unless otherwise stated, the command line tools that need a tree take a filepath 
to a newick tree file. The numeric suffix of each label in the tree is taken to
be the OTT id. This accommodates the name munging that some of the open tree of
life tools perform on taxonomic names with special characters (because only the
OTT id is used to associate labels in different trees)

### Checking for incorrect internal labels in a full tree

    otcchecktaxonomicnodes synth.tre taxonomy.tre

will check every labelled internal node is correctly labelled. To do this, it 
verifies that the set of OTT ids associated with tips that descend from the 
node is identical to the set of OTT ids associated with terminal taxa below
the corresponding node in the taxonomic tree.

A report will be issued for every problematic labeling. 

Assumptions:
  1. synth tree and taxonomy tree have the same leaf set in terms of OTT ids
  2. each label has numeric suffix, which is treated as the OTT id.

## ACKNOWLEDGEMENTS
See comments above about usage of [easyloggingpp](https://github.com/easylogging/)
and [rapidjson](https://github.com/miloyip/rapidjson)

To acknowledge the contributions of the NCL code and ideas, a snapshot of the 
NCL credits taken from the version of NCL used to jump start otcetera is:

As of March 09, 2012, NCL is available under a Simplified BSD license (see
BSDLicense.txt) in addition to the GPL license.

NCL AUTHORS -- the author of the NEXUS Class Library (NCL) version 2.0 is

  Paul O. Lewis, Ph.D.
  Department of Ecology and Evolutionary Biology
  The University of Connecticut
  75 North Eagleville Road, Unit 3043
  Storrs, CT 06269-3043
  U.S.A.

  WWW: http://lewis.eeb.uconn.edu/lewishome
  Email: paul.lewis@uconn.edu


Versions after 2.0 contain changes primarily made by:
  Mark T. Holder  mholder@users.sourceforge.net

Other contributors to these versions include:
  Derrick Zwickl
  Brian O'Meara
  Brandon Chisham
  Fran�ois Michonneau
  Jeet Sukumaran

The code in examples/phylobase... was written by Brian O'Meara and Derrick Zwickl
for phylobase.

David Su�rez Pascal contributed SWIG bindings which heavily influenced those
   found in branches/v2.2. Thanks to David for blazing the way on the swig binding,
    Google for funding, and NESCent (in particular Hilmar Lapp) for getting the
    NESCent GSoC program going.

The 2010 GSoC effort also led to enhancements in terms of annotation storage and
xml parsing which are currently on. Michael Elliot contributed some code to the branches/xml branch.
Thanks to NESCent and  Google for supporting that work.

Many of the files used for testing were provided by Arlin Stoltzfus (see
http://www.molevol.org/camel/projects/nexus/ for more information), the Mesquite
package, and from TreeBase (thanks, Bill Piel!).

