/*
   This file is part of Fjalar, a dynamic analysis framework for C/C++
   programs.

   Copyright (C) 2004-2005 Philip Guo, MIT CSAIL Program Analysis Group

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
*/


/* fjalar_select.c:

Implements selective tracing of particular program points and
variables.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>

#include "fjalar_main.h"
#include "fjalar_select.h"
#include "generate_fjalar_entries.h"

// Output file pointer for dumping program point names
FILE* prog_pt_dump_fp = 0;
// Output file pointer for dumping variable names
FILE* var_dump_fp = 0;

// Input file pointer for list of program points to trace
FILE* trace_prog_pts_input_fp = 0;
// Input file pointer for list of variables to trace
FILE* trace_vars_input_fp = 0;

const char COMMENT_CHAR = '#';
const char* ENTRY_DELIMETER = "----SECTION----";
const int ENTRY_DELIMETER_LEN = 15;
const char* GLOBAL_STRING = "globals";
const int GLOBAL_STRING_LEN = 7;
const char* MANGLED_TOKEN = "(mangled)";


// Temporary scratch buffer for reading lines in from files
//  TODO: This is crude and unsafe but works for now
char input_line[200];

// GNU binary tree (built into search.h) (access using tsearch and
// tfind) which holds either the full Fjalar name or the mangled name
// (for C++ only) of the program points which we are interested in
// tracing.  When we are trying to detect whether to instrument a
// particular FunctionEntry at translation-time within
// handle_possible_entry() and handle_possible_exit(), we know whether
// to look for the Fjalar name or the mangled C++ name by simply
// querying whether the particular entry has a mangled C++ name.  If
// it has one, we should use it; otherwise, use the Fjalar name.
char* prog_pts_tree = NULL;

// GNU binary tree that holds names of functions and trees of variables
// to trace within those functions (packed into a FunctionTree struct)
FunctionTree* vars_tree = NULL;

// Special entry for global variables
FunctionTree* globalFunctionTree = 0;

// TODO: Warning! We never free the memory used by prog_pts_tree and vars_tree
// but don't worry about it for now


// Iterate through each line of the file trace_prog_pts_input_fp
// and VG_(strdup) each string into a new entry of prog_pts_tree
// Every line in a ppt program point file must be one of the following:
//
//  1.) A full Fjalar name of the program point (as printed in
//  .decls/.dtrace) - This happens most of the time.
//
// e.g.:   FunctionNamesTest.c.staticFoo()
//
//  2.) The keyword '(mangled)' followed by the mangled program point
//  name then followed by the full Fjalar name, all separated by
//  spaces.  This only happens for C++ function names that are
//  mangled.  The mangled name is what Fjalar actually uses and the
//  Fjalar name is just there for human readability
//
// e.g.:   (mangled) _Z17firstFileFunctionv ..firstFileFunction()
//
//
// A list of addresses/program point names can be generated by running Fjalar
// with the --dump-ppt-file=<string> command-line option)
// Close the file when you're done with it
//
// (Comments are allowed - ignore all lines starting with COMMENT_CHAR)
//
// Here is an example of a program point list file with both regular
// and mangled names:
//
//     FunctionNamesTest.c.staticFoo()
//     (mangled) _Z17firstFileFunctioni ..firstFileFunction(int)
//     ..main()
//     second_file.c.staticFoo()
//     (mangled) _Z18secondFileFunctionv ..secondFileFunction()
//
void initializeProgramPointsTree()
{
  while (fgets(input_line, 200, trace_prog_pts_input_fp))
    {
      char *newString;
      char *firstToken;
      int lineLen = VG_(strlen)(input_line);

      // Skip blank lines (those consisting of solely the newline character)
      // Also skip comment lines (those beginning with COMMENT_CHAR)
      if(('\n' == input_line[0]) ||
         (COMMENT_CHAR == input_line[0])) {
        //        VG_(printf)("skipping blank line ...\n");
        continue;
      }

      // Strip '\n' off the end of the line
      // NOTE: Only do this if the end of the line is a newline character.
      // If the very last line of a file is not followed by a newline character,
      // then blindly stripping off the last character will truncate the actual
      // string, which is undesirable.
      if (input_line[lineLen - 1] == '\n') {
        input_line[lineLen - 1] = '\0';
      }

      newString = VG_(strdup)(input_line);

      // Check out the first token
      firstToken = strtok(newString, " ");

      // If it matches MANGLED_TOKEN, then we are dealing with a mangled
      // C++ name so we just grab it as the second token
      if (0 == VG_(strcmp)(firstToken, MANGLED_TOKEN)) {
        char* secondToken = strtok(NULL, " ");
        //        VG_(printf)("mangled: %s\n", secondToken);
        tsearch((void*)VG_(strdup)(secondToken), (void**)&prog_pts_tree, compareStrings);
      }
      // Otherwise, that is the Fjalar name of the function so grab it
      else {
        //        VG_(printf)("regular: %s\n", firstToken);
        tsearch((void*)VG_(strdup)(firstToken), (void**)&prog_pts_tree, compareStrings);
      }

      VG_(free)(newString);
    }

  fclose(trace_prog_pts_input_fp);
  trace_prog_pts_input_fp = 0;
}


// Iterate through each line of the file trace_vars_input_fp
// and insert the line below ENTRY_DELIMETER into vars_tree as
// a new FunctionTree.  Then iterate through all variables within that function
// and add them to a tree of strings in FunctionTree.variable_tree
// Close the file when you're done
//
// (Comments are allowed - ignore all lines starting with COMMENT_CHAR)
//
/* This is an example of a variables output format:

----SECTION----
globals
StaticArraysTest_c/staticStrings
StaticArraysTest_c/staticStrings[]
StaticArraysTest_c/staticShorts
StaticArraysTest_c/staticShorts[]

----SECTION----
..f()
arg
strings
strings[]
return


----SECTION----
..b()
oneShort
manyShorts
manyShorts[]
return

----SECTION----
..main()
return

*/
void initializeVarsTree()
{
  char nextLineIsFunction = 0;
  FunctionTree* currentFunctionTree = 0;
  while (fgets(input_line, 200, trace_vars_input_fp))
    {
      int lineLen = VG_(strlen)(input_line);

      // Skip blank lines (those consisting of solely the newline character)
      // Also skip comment lines (those beginning with COMMENT_CHAR)
      if(('\n' == input_line[0]) ||
         (COMMENT_CHAR == input_line[0])) {
        //        VG_(printf)("skipping blank line ...\n");
        continue;
      }

      // Strip '\n' off the end of the line
      // NOTE: Only do this if the end of the line is a newline character.
      // If the very last line of a file is not followed by a newline character,
      // then blindly stripping off the last character will truncate the actual
      // string, which is undesirable.
      if (input_line[lineLen - 1] == '\n') {
        input_line[lineLen - 1] = '\0';
      }

      // For some weird reason, it crashes when you don't use strncmp
      if (VG_(strncmp)(input_line, ENTRY_DELIMETER, ENTRY_DELIMETER_LEN) == 0)
	{
	  nextLineIsFunction = 1;
	}
      else
	{
	  // Create a new FunctionTree and insert it into vars_tree
	  if (nextLineIsFunction)
	    {
	      currentFunctionTree = VG_(malloc)(sizeof(*currentFunctionTree));
	      currentFunctionTree->function_fjalar_name = VG_(strdup)(input_line);
	      currentFunctionTree->function_variables_tree = NULL; // Remember to initialize to null!

	      tsearch((void*)currentFunctionTree, (void**)&vars_tree, compareFunctionTrees);

	      // Keep a special pointer for global variables to trace

              // For some weird reason, it crashes when you don't use strncmp
              if (VG_(strncmp)(input_line, GLOBAL_STRING, GLOBAL_STRING_LEN) == 0)
		{
		  globalFunctionTree = currentFunctionTree;
                  //                  VG_(printf)("globalFunctionTree: %p\n", globalFunctionTree);
		}
	    }
	  // Otherwise, create a new variable and stuff it into
	  // the function_variables_tree of the current function_tree
	  else
	    {
	      char* newString = VG_(strdup)(input_line);
	      tsearch((void*)newString, (void**)&(currentFunctionTree->function_variables_tree), compareStrings);
              //              VG_(printf)("variable: %s\n", newString);
	    }

	  nextLineIsFunction = 0;
	}
    }

  fclose(trace_vars_input_fp);
  trace_vars_input_fp = 0;
}


// Returns 1 if the proper function name of cur_entry is found in
// prog_pts_tree and 0 otherwise.  If cur_entry->mangled_name exists,
// that is what we look for; otherwise, we look for
// cur_entry->fjalar_name.
char prog_pts_tree_entry_found(FunctionEntry* cur_entry) {

  char* nameToFind = (cur_entry->mangled_name ?
                      cur_entry->mangled_name : cur_entry->fjalar_name);

  if (tfind((void*)nameToFind,
            (void**)&prog_pts_tree,
            compareStrings)) {
    return 1;
  }
  else {
    return 0;
  }
}

// Compares their names
int compareFunctionTrees(const void *a, const void *b)
{
  return VG_(strcmp)(((FunctionTree*) a)->function_fjalar_name,
		     ((FunctionTree*) b)->function_fjalar_name);
}

int compareStrings(const void *a, const void *b)
{
  return VG_(strcmp)((char*)a, (char*)b);
}
