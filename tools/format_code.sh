#!/bin/bash

cd ../
SUBDIRS="include src examples "
FILETYPES="*.c *.h *.cpp *.hpp"
ASTYLE="astyle -A2 -HtUwpj -M80 -c -s2 --pad-header --align-pointer=type "
for d in ${SUBDIRS}
do
  for t in ${FILETYPES}
  do
    for file in $d/$t
    do
      if test -f $file
      then
        ${ASTYLE} $file 
        rm -f ${file}.orig
      fi
    done
  done
done
