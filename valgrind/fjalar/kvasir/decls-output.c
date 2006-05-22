/*
   This file is part of Kvasir, a C/C++ front end for the Daikon
   dynamic invariant detector built upon the Fjalar framework

   Copyright (C) 2004-2006 Philip Guo (pgbovine@alum.mit.edu),
   MIT CSAIL Program Analysis Group

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
*/

/* decls-output.c:

   Functions for creating .decls and .dtrace files and outputting
   name and type information into a Daikon-compatible .decls file

*/

#include "decls-output.h"
#include "kvasir_main.h"
#include "dyncomp_runtime.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <limits.h>

const char* ENTER_PPT = ":::ENTER";
const char* EXIT_PPT = ":::EXIT0";

extern const char* DeclaredTypeString[];

// This array can be indexed using the DaikonRepType enum
static const char* DaikonRepTypeString[] = {
  "no_rep_type", //R_NO_TYPE, // Create padding
  "int", //R_INT,
  "double", //R_DOUBLE,
  "hashcode", //R_HASHCODE,
  "java.lang.String", //R_STRING
  "boolean" //R_BOOL
};


// Use this function to print out a function name for .decls/.dtrace.
void printDaikonFunctionName(FunctionEntry* f, FILE* fp) {
  if (kvasir_new_decls_format) {
    char* name = 0;
    Bool appendParens = False;

    // Don't use the Fjalar name because it's a real mess with lots of
    // extra unnecessary stuff.

    // If there is a C++ demangled name, then use it:
    if (f->demangled_name) {
      name = f->demangled_name;
    }
    // Otherwise, just use the regular C name but append '()' to the
    // end of it:
    else {
      name = f->name;
      appendParens = True;
    }

    // Spaces in program point names must be backslashed,
    // so change ' ' to '\ '.

    // Backslashes should be double-backslashed,
    // so change '\' to '\\'.

    while (*name != '\0') {
      if (*name == ' ') {
        fputs("\\ ", fp);
      }
      else if (*name == '\\') {
        fputs("\\\\", fp);
      }
      else {
        fputc(*name, fp);
      }
      name++;
    }

    if (appendParens) {
      fputs("()", fp);
    }
  }
  else {
    fputs(f->fjalar_name, fp);
  }
}


// Converts a variable name given by Fjalar into a Daikon external
// name and prints it out to FILE*.  Currently, two changes need to be made:
//   1. Change '[]' into '[..]' for array indexing.  However, we
//      should only change the first instance of '[]' because Daikon
//      only currently supports one level of sequences.
//   2. Change ' ' to '\ ' (spaces to backslash-space) and '\' to '\\'
//      (backslash to double-backslash)
//   3. Change the leading '/' that Fjalar uses to denote global variables
//      to '::' in order to be compatible with C++ syntax.
//      (e.g., change "/foo" to "::foo")
//   4. Strip off everything before the LAST '/' for a global variable to
//      eliminate all disambiguation information for static variables.
//      (e.g., change "custom-dir/ArrayTest_c@returnIntSum/static_local_array"
//       to "::static_local_array".  In this example, static_local_array is
//       a static variable declared within the returnIntSum() function of
//       the file 'custom-dir/ArrayTest.c'.)
static void printDaikonExternalVarName(char* fjalarName, FILE* fp) {
  int indexOfLastSlash = -1;
  int len = VG_(strlen)(fjalarName);
  int i;
  char* working_name = 0;
  Bool alreadyPrintedBrackets = False; // Only print out one set of "[..]" max.

  for (i = 0; i < len; i++) {
    if (fjalarName[i] == '/') {
      indexOfLastSlash = i;
    }
  }

  if (indexOfLastSlash >= 0) {
    // Ignore everything before the final slash:
    working_name = &fjalarName[indexOfLastSlash];
  }
  // No slashes found, just use the name as is
  else {
    working_name = fjalarName;
  }

  // Special case for printing out leading '/' as '::':
  if (*working_name == '/') {
    fputs("::", fp);
    working_name++;
  }

  while (*working_name != '\0') {
    if ((*working_name == '[') &&
        (*(working_name + 1) == ']') &&
        !alreadyPrintedBrackets) {
      fputs("[..", fp);
      alreadyPrintedBrackets = True;
    }
    else if (*working_name == ' ') {
        fputs("\\ ", fp);
    }
    else if (*working_name == '\\') {
      fputs("\\\\", fp);
    }
    // Default ... simply output the current character
    else {
      fputc(*working_name, fp);
    }

    working_name++;
  }
}


static char* createDaikonExternalVarName(char* fjalarName) {
  // TODO: Yes, I know that regexps will work, but the overhead of
  // figuring out how to do them in C is prohibitive (I've already
  // spent 10 minutes reading the docs for regex.h, and I haven't
  // found a 'regexp find-and-replace' function yet, so I'm giving up)

  int len = VG_(strlen)(fjalarName), i;
  int bracketsIndex = -1;

  int numSpaces = 0;
  int numBackslashes = 0;

  char* result = 0;

  // Try to look for a set of brackets
  for (i = 0; i < len; i++) {
    if (fjalarName[i] == '[') {
      if (fjalarName[i+1] == ']') {
        bracketsIndex = i;
        break;
      }
    }
  }

  // Try to look for spaces or backslashes (should be rare)
  for (i = 0; i < len; i++) {
    if (fjalarName[i] == ' ') {
      numSpaces++;
    }
    else if (fjalarName[i] == '\\') {
      numBackslashes++;
    }
  }

  // Replace '[]' with '[..]'
  if (bracketsIndex >= 0) {
    // '..' is of length 2, remember 1 extra for '\0' null terminator
    result = VG_(malloc)((len + 3) * sizeof(*result));

    // Copy everything up to the brackets
    VG_(memcpy)(result, fjalarName, (bracketsIndex + 1));
    // Insert '..'
    result[bracketsIndex + 1] = '.';
    result[bracketsIndex + 2] = '.';
    // Copy everything up to the end of the string
    VG_(memcpy)(&result[bracketsIndex + 3], &fjalarName[bracketsIndex + 1], (len - bracketsIndex - 1));

    // Cap it off with a '\0'
    result[len + 2] = '\0';
  }
  else {
    // We should still allocate a new string regardless because the
    // client expects it (may be a bit inefficient, but oh well ...)
    result = VG_(strdup)(fjalarName);
  }

  // Replace ' ' with '\ ' and '\' with '\\'
  if ((numSpaces > 0) || (numBackslashes > 0)) {
    int result_len = VG_(strlen)(result);
    char* new_result = VG_(malloc)((result_len + numSpaces + numBackslashes + 1) * sizeof(*new_result));
    char* result_alias = result;
    char* new_result_alias = new_result;

    // Let's do this old-school UNIX style ...

    // Iterate down result until it ends, replacing ' ' with '\ ' and
    // '\' with '\\'
    while (*result_alias != '\0') {
      if (*result_alias == ' ') {
        *new_result_alias = '\\';
        new_result_alias++;
        *new_result_alias = ' ';
        new_result_alias++;
      }
      else if (*result_alias == '\\') {
        *new_result_alias = '\\';
        new_result_alias++;
        *new_result_alias = '\\';
        new_result_alias++;
      }
      else {
        *new_result_alias = *result_alias;
        new_result_alias++;
      }

      result_alias++;
    }

    // Cap it off ...
    *new_result_alias = '\0';

    return new_result;
  }
  else {
    return result;
  }
}

static void printDeclsHeader(void);
static void printAllFunctionDecls(char faux_decls);
static void printAllObjectPPTDecls(void);

// This has different behavior depending on if faux_decls is on.  If
// faux_decls is on, then we do all the processing but don't actually
// output anything to the .decls file.
void outputDeclsFile(char faux_decls)
{
  // Punt if you are not printing declarations at all:
  if (!print_declarations) {
    return;
  }

  if (!faux_decls) {
    printDeclsHeader();
  }

  printAllFunctionDecls(faux_decls);

  // For DynComp, print this out at the end of execution
  if (!kvasir_with_dyncomp) {
    printAllObjectPPTDecls();
  }

  // Clean-up:
  // Only close decls_fp if we are generating it separate of .dtrace

  if (!faux_decls) {
    if (actually_output_separate_decls_dtrace) {
      fclose(decls_fp);
      decls_fp = 0;
    }
  }
}

// Print .decls at the end of program execution and then close it
// (Only used when DynComp is on)
void DC_outputDeclsAtEnd() {
  //  VG_(printf)("DC_outputDeclsAtEnd()\n");
  printAllFunctionDecls(0);

  printAllObjectPPTDecls();

  fclose(decls_fp);
  decls_fp = 0;
}


// Converts a Fjalar DeclaredType into a Daikon rep. type:
DaikonRepType decTypeToDaikonRepType(DeclaredType decType,
                                     char isString) {
  if (isString) {
    return R_STRING;
  }

  switch (decType) {
  case D_UNSIGNED_CHAR:
  case D_CHAR:
  case D_UNSIGNED_SHORT:
  case D_SHORT:
  case D_UNSIGNED_INT:
  case D_INT:
  case D_UNSIGNED_LONG_LONG_INT:
  case D_LONG_LONG_INT:
  case D_ENUMERATION:
    return R_INT;

  case D_BOOL:            // C++ only
    return R_BOOLEAN;

  case D_FLOAT:
  case D_DOUBLE:
  case D_LONG_DOUBLE:
    return R_DOUBLE;

  case D_STRUCT_CLASS:
  case D_UNION:
  case D_FUNCTION:
  case D_VOID:
    return R_HASHCODE;

  case D_CHAR_AS_STRING: // when .disambig 'C' option is used with chars
    return R_STRING;

  default:
    tl_assert(0);
    return R_NO_TYPE;
  }
}

// Do absolutely nothing but keep on letting Fjalar traverse over all
// of the variables.  This is used by DynComp to see how many
// variables (both actual and derived) are present at a program point
// (g_variableIndex increments during each variable visit).
TraversalResult nullAction(VariableEntry* var,
                           char* varName,
                           VariableOrigin varOrigin,
                           UInt numDereferences,
                           UInt layersBeforeBase,
                           char overrideIsInit,
                           DisambigOverride disambigOverride,
                           char isSequence,
                           // pValue only valid if isSequence is false
                           void* pValue,
                           // pValueArray and numElts only valid if
                           // isSequence is true
                           void** pValueArray,
                           UInt numElts,
                           FunctionEntry* varFuncInfo,
                           char isEnter) {
  return DISREGARD_PTR_DEREFS;
}


// This is where all of the action happens!
// Print a .decls entry for a particular variable:
// Pre: varName is allocated and freed by caller
// This consists of 4 lines:
// var. name, declared type, rep. type, comparability number
// e.g.,
// /foo                 <-- variable name
// char*                <-- declared type
// java.lang.String     <-- rep. type
// 22                   <-- comparability number
TraversalResult printDeclsEntryAction(VariableEntry* var,
                                      char* varName,
                                      VariableOrigin varOrigin,
                                      UInt numDereferences,
                                      UInt layersBeforeBase,
                                      Bool overrideIsInit,
                                      DisambigOverride disambigOverride,
                                      Bool isSequence,
                                      // pValue only valid if isSequence is false
                                      void* pValue,
                                      // pValueArray and numElts only valid if
                                      // isSequence is true
                                      void** pValueArray,
                                      UInt numElts,
                                      FunctionEntry* varFuncInfo,
                                      Bool isEnter) {
  DeclaredType dType = var->varType->decType;
  DaikonRepType rType = decTypeToDaikonRepType(dType, var->isString);
  UInt layers;
  char printingFirstAnnotation = 1;
  char alreadyPutDerefOnLine3;

  char printAsSequence = isSequence;

  if (kvasir_new_decls_format) {
    int len = VG_(strlen)(varName);

    // Boolean flags for variables:
    // TODO: Add more flags later as necessary
    Bool is_param_flag = False;
    Bool non_null_flag = False;

    // The format: (entries in brackets are optional, indentation
    //              doesn't matter)
    //
    //      variable <external-name>
    //        var-kind <variable-kinds>
    //        [enclosing-var <external-name>]
    //        [reference-type pointer|offset]
    //        [array <dim-cnt>]
    //        [function-args <arg-list>]
    //        rep-type <representation-type>
    //        dec-type <declared-type>
    //        [flags <variable-flags>]
    //        [lang-flags <language-specific-flags>]
    //        [parent <parent-ppt-name> [<parent-var-name>]]
    //        [comparability <comparability-value>]

    // ****** External variable name ******
    fputs("  variable ", decls_fp);
    printDaikonExternalVarName(varName, decls_fp);
    fputs("\n", decls_fp);

    // ****** Variable kind ******

    fputs("    var-kind ", decls_fp);

    // For a very special case where the suffix of the variable is
    // ZEROTH_ELT ("[0]"), that represents a pointer deference, so we
    // will represent it as of kind 'function *'.  Thus, for variable
    // "foo[0]", the var-kind will be "function *" because it's
    // equivalent to applying the deference * operator on the variable
    // foo.
    if ((varName[len - 3] == '[') &&
        (varName[len - 2] == '0') &&
        (varName[len - 1] == ']')) {
      fputs("function *", decls_fp);
    }
    // If numDereferences > 0, then it's an array variable that's the
    // result of the dereference of either a field or a top-level
    // variable:
    else if (numDereferences > 0) {
      fputs("array", decls_fp);
    }
    else if (IS_MEMBER_VAR(var)) {
      fputs("field ", decls_fp);
      // Print out just this variable's name as the field name
      fputs(var->name, decls_fp);
    }
    else {
      fputs("variable", decls_fp);
    }

    fputs("\n", decls_fp);

    // ****** Enclosing variable (optional) ******

    // There is an enclosing variable if enclosingVarNamesStackSize > 0
    if (enclosingVarNamesStackSize > 0) {

      fputs("    enclosing-var ", decls_fp);
      printDaikonExternalVarName(enclosingVarNamesStack[enclosingVarNamesStackSize - 1],
                                 decls_fp);
      fputs("\n", decls_fp);
    }

    // ****** Reference type (optional) ******

    // ****** Array dimensions (optional) ******

    // Note that currently Daikon does not support more than 1 level
    // of sequences so the only possible (non-default) value for this
    // is 'array 1'.
    if (isSequence) {
      fputs("    array 1\n", decls_fp);
    }

    // ****** Rep. type ******
    fputs("    rep-type ", decls_fp);

    // This code is copied and pasted from code in the 'else' branch
    // (old .decls format)
    alreadyPutDerefOnLine3 = 0;

    // Print out rep. type as hashcode when you are not done dereferencing
    // pointer layers:
    if (layersBeforeBase > 0) {
      fputs(DaikonRepTypeString[R_HASHCODE], decls_fp);
    }
    else {
      // Special handling for strings and 'C' chars in .disambig
      if (OVERRIDE_STRING_AS_INT_ARRAY == disambigOverride) {
        fputs(DaikonRepTypeString[R_INT], decls_fp);
        fputs(DEREFERENCE, decls_fp);
        alreadyPutDerefOnLine3 = 1;
      }
      else if (OVERRIDE_STRING_AS_ONE_INT == disambigOverride) {
        fputs(DaikonRepTypeString[R_INT], decls_fp);
      }
      else if ((var->isString) ||
               (OVERRIDE_CHAR_AS_STRING == disambigOverride)) {
        // TODO: Change this permanently from "java.lang.String" to
        // "string" in DaikonRepTypeString[] when we're done with the
        // switch-over to the new .decls format
        fputs("string", decls_fp);
      }
      else {
        tl_assert(rType != 0);
        fputs(DaikonRepTypeString[rType], decls_fp);
      }
    }

    // Append "[]" onto the end of the rep. type if necessary
    if (!alreadyPutDerefOnLine3 &&
        printAsSequence) {
      fputs(DEREFERENCE, decls_fp);
    }

    fputs("\n", decls_fp);

    // ****** Declared type ******
    fputs("    dec-type ", decls_fp);

    // This code is copied and pasted from code in the 'else' branch
    // (old .decls format)
    if (OVERRIDE_STRING_AS_INT_ARRAY == disambigOverride) {
      fputs(DaikonRepTypeString[R_INT], decls_fp);
      fputs(DEREFERENCE, decls_fp);
    }
    else if (OVERRIDE_STRING_AS_ONE_INT == disambigOverride) {
      fputs(DaikonRepTypeString[R_INT], decls_fp);
    }
    // named struct/union or enumeration
    else if (((dType == D_ENUMERATION) || (dType == D_STRUCT_CLASS) || (dType == D_UNION)) &&
             var->varType->typeName) {
      fputs(var->varType->typeName, decls_fp);
    }
    else {
      // Normal type (or unnamed struct/union/enum)
      fputs(DeclaredTypeString[dType], decls_fp);
      // If we have a string, print it as char* because the dType of
      // string is "char" so we need to append a "*" to it
      if (var->isString) {
        fputs(STAR, decls_fp);
      }
    }

    // For the declared type, print out one level of '*' for every
    // layer above base to denote pointer types
    for (layers = 0; layers < layersBeforeBase; layers++) {
      fputs(STAR, decls_fp);
    }

    // If we print this as a sequence, then we must append '[]'
    if (printAsSequence) {
      fputs(DEREFERENCE, decls_fp);
    }

    fputs("\n", decls_fp);


    // ****** Flags ****** (optional)
    if (varOrigin == FUNCTION_FORMAL_PARAM) {
      is_param_flag = True;
    }

    if (IS_STATIC_ARRAY_VAR(var) && (layersBeforeBase == 1)) {
      non_null_flag = True;
    }


    // Only print out optional flags line if at least one flag is True
    if (is_param_flag || non_null_flag /* || other flags ... */) {
      fputs("    flags ", decls_fp);

      if (is_param_flag) {
        fputs("is_param ", decls_fp);
      }

      if (non_null_flag) {
        fputs("non_null ", decls_fp);
      }
      // TODO: Add output for other supported flags

      fputs("\n", decls_fp);
    }

    // ****** Comparability ****** (optional)

    // If we are outputting a REAL .decls with DynComp, that means
    // that the program has already finished execution so that all
    // of the comparability information would be already updated:
    if (kvasir_with_dyncomp) {
      // Remember that comp_number is a SIGNED INTEGER but the
      // tags are UNSIGNED INTEGERS so be careful of overflows
      // which result in negative numbers, which are useless
      // since Daikon ignores them.
      int comp_number = DC_get_comp_number_for_var((DaikonFunctionEntry*)varFuncInfo,
                                                   isEnter,
                                                 g_variableIndex);
      fputs("    comparability ", decls_fp);
      fprintf(decls_fp, "%d", comp_number);
      fputs("\n", decls_fp);
    }
  }
  else {
    // Line 1: Variable name
    fprintf(decls_fp, "%s\n", varName);

    // Line 2: Declared type
    if (OVERRIDE_STRING_AS_INT_ARRAY == disambigOverride) {
      fputs(DaikonRepTypeString[R_INT], decls_fp);
      fputs(DEREFERENCE, decls_fp);
    }
    else if (OVERRIDE_STRING_AS_ONE_INT == disambigOverride) {
      fputs(DaikonRepTypeString[R_INT], decls_fp);
    }
    // named struct/union or enumeration
    else if (((dType == D_ENUMERATION) || (dType == D_STRUCT_CLASS) || (dType == D_UNION)) &&
             var->varType->typeName) {
      fputs(var->varType->typeName, decls_fp);
    }
    else {
      // Normal type (or unnamed struct/union/enum)
      fputs(DeclaredTypeString[dType], decls_fp);
      // If we have a string, print it as char* because the dType of
      // string is "char" so we need to append a "*" to it
      if (var->isString) {
        fputs(STAR, decls_fp);
      }
    }

    // For the declared type, print out one level of '*' for every
    // layer above base to denote pointer types
    for (layers = 0; layers < layersBeforeBase; layers++) {
      fputs(STAR, decls_fp);
    }

    // If we print this as a sequence, then we must append '[]'
    if (printAsSequence) {
      fputs(DEREFERENCE, decls_fp);
    }

    // Add annotations as comments in .decls file
    // (The first one is preceded by ' # ' and all subsequent ones are
    // preceded by ',')

    // Original vars in function parameter lists have "isParam=true"

    if (varOrigin == FUNCTION_FORMAL_PARAM) {
      if (printingFirstAnnotation) {fputs(" # ", decls_fp);}
      else {fputs(",", decls_fp);}

      fputs("isParam=true", decls_fp);
    }

    // Struct variables are annotated with "isStruct=true"
    // in order to notify Daikon that the hashcode values printed
    // out for that variable have no semantic meaning
    if (fjalar_output_struct_vars &&
        (layersBeforeBase == 0) &&
        (IS_AGGREGATE_TYPE(var->varType))) {
      if (printingFirstAnnotation) {fputs(" # ", decls_fp);}
      else {fputs(",", decls_fp);}

      fputs("isStruct=true", decls_fp);
    }

    // Hashcode variables that can never be null has "hasNull=false".
    // (e.g., statically-allocated arrays)
    if (IS_STATIC_ARRAY_VAR(var) && (layersBeforeBase == 1)) {
      if (printingFirstAnnotation) {fputs(" # ", decls_fp);}
      else {fputs(",", decls_fp);}

      fputs("hasNull=false", decls_fp);
    }

    fputs("\n", decls_fp);


    // Line 3: Rep. type
    alreadyPutDerefOnLine3 = 0;

    // Print out rep. type as hashcode when you are not done dereferencing
    // pointer layers:
    if (layersBeforeBase > 0) {
      fputs(DaikonRepTypeString[R_HASHCODE], decls_fp);
    }
    else {
      // Special handling for strings and 'C' chars in .disambig
      if (OVERRIDE_STRING_AS_INT_ARRAY == disambigOverride) {
        fputs(DaikonRepTypeString[R_INT], decls_fp);
        fputs(DEREFERENCE, decls_fp);
        alreadyPutDerefOnLine3 = 1;
      }
      else if (OVERRIDE_STRING_AS_ONE_INT == disambigOverride) {
        fputs(DaikonRepTypeString[R_INT], decls_fp);
      }
      else if ((var->isString) ||
               (OVERRIDE_CHAR_AS_STRING == disambigOverride)) {
        fputs(DaikonRepTypeString[R_STRING], decls_fp);
      }
      else {
        tl_assert(rType != 0);
        fputs(DaikonRepTypeString[rType], decls_fp);
      }
    }

    // Append "[]" onto the end of the rep. type if necessary
    if (!alreadyPutDerefOnLine3 &&
        printAsSequence) {
      fputs(DEREFERENCE, decls_fp);
    }

    fputs("\n", decls_fp);


    // Line 4: Comparability number

    // If we are outputting a REAL .decls with DynComp, that means
    // that the program has already finished execution so that all
    // of the comparability information would be already updated:
    if (kvasir_with_dyncomp) {
      // Remember that comp_number is a SIGNED INTEGER but the
      // tags are UNSIGNED INTEGERS so be careful of overflows
      // which result in negative numbers, which are useless
      // since Daikon ignores them.
      int comp_number = DC_get_comp_number_for_var((DaikonFunctionEntry*)varFuncInfo,
                                                   isEnter,
                                                 g_variableIndex);
      fprintf(decls_fp, "%d", comp_number);
      fputs("\n", decls_fp);
    }
    else {
      // Otherwise, print out unknown comparability type "22"
      fputs("22", decls_fp);
      fputs("\n", decls_fp);
    }
  }


  // We are done!
  return DISREGARD_PTR_DEREFS;
}

// Print out the Daikon .decls header depending on whether DynComp is
// activated or not
static void printDeclsHeader(void)
{
  if (kvasir_new_decls_format) {
    // These are the global records that go at the top of the .decls file

    // TODO: Make separate flags for C and C++; right now this simply
    // prints C/C++.  This information can be grabbed from the DWARF2
    // debug. info. using the DW_AT_language tags (try "readelf -w" on
    // the target program's binary to see what I mean)
    fputs("input-language C/C++\n", decls_fp);
    if (kvasir_with_dyncomp) {
      fputs("var-comparability implicit\n", decls_fp);
    }
    else {
      fputs("var-comparability none\n", decls_fp);
    }
    fputs("\n", decls_fp);
  }
  else {
    if (kvasir_with_dyncomp) {
      // VarComparability implicit is the DEFAULT so we don't need to
      // write anything here:
      //    fputs("VarComparability\nimplicit\n\n", decls_fp);
    }
    else {
      fputs("VarComparability\nnone\n\n", decls_fp);
    }
  }
}

// Print out one individual function declaration
// Example:
/*
DECLARE
printHelloWorld():::ENTER
routebaga
double # isParam=true
double
1
turnip
char # isParam=true
int
2
*/
// char isEnter = 1 for function ENTER, 0 for EXIT
// faux_decls = True if we are making the FIRST pass with DynComp to count up
// how many Daikon variables exist at a program point so that we can initialize
// the proper data structures (no .decls output is made during this dry run)
// and faux_decls = False if we are really outputting .decls, which is in the
// beginning of execution without DynComp and at the END of execution with DynComp
void printOneFunctionDecl(FunctionEntry* funcPtr,
                          char isEnter,
                          char faux_decls) {
  // This is a GLOBAL so be careful :)
  // Reset it before doing any traversals
  g_variableIndex = 0;

  if (!faux_decls) {
    if (kvasir_new_decls_format) {
      fputs("ppt ", decls_fp);
      printDaikonFunctionName(funcPtr, decls_fp);
      fputs("\n  ppt-type ", decls_fp);
      if (isEnter) {
        fputs("enter\n", decls_fp);
      }
      else{
        fputs("exit\n", decls_fp);
      }
      // If it's a member function, then print out its parent, which
      // is the object program point of its enclosing class:
      if (funcPtr->parentClass && funcPtr->parentClass->typeName) {
        fputs("  parent ", decls_fp);
        fputs(funcPtr->parentClass->typeName, decls_fp);
        fputs("\n", decls_fp);
      }
    }
    else {
      fputs("DECLARE\n", decls_fp);
      printDaikonFunctionName(funcPtr, decls_fp);

      if (isEnter) {
        fputs(ENTER_PPT, decls_fp);
        fputs("\n", decls_fp);
      }
      else {
        fputs(EXIT_PPT, decls_fp);
        fputs("\n", decls_fp);
      }
    }

    // For outputting real .decls when running with DynComp,
    // initialize a global hashtable which associates tags with
    // sequentially-assigned comparability numbers
    if (kvasir_with_dyncomp) {
      // This is a GLOBAL so be careful :)
      g_compNumberMap = genallocatehashtable(NULL, // no hash function needed for u_int keys
                                             (int (*)(void *,void *)) &equivalentIDs);

      g_curCompNumber = 1;

      if (dyncomp_detailed_mode) {
        DC_convert_bitmatrix_to_sets(funcPtr, isEnter);
      }
    }
  }

  // Print out globals (visitVariableGroup() ignores the globals if
  // --ignore-globals is used):
  visitVariableGroup(GLOBAL_VAR,
                     funcPtr, // need this for DynComp to work properly
                     isEnter,
                     0,
                     (faux_decls ?
                      &nullAction : &printDeclsEntryAction));

  // Now print out one entry for every formal parameter (actual and derived)
  visitVariableGroup(FUNCTION_FORMAL_PARAM,
                     funcPtr,
                     isEnter,
                     0,
                     (faux_decls ?
                      &nullAction : &printDeclsEntryAction));

  // If EXIT, print out return value
  if (!isEnter) {
    visitVariableGroup(FUNCTION_RETURN_VAR,
                       funcPtr,
                       0,
                       0,
                       (faux_decls ?
                        &nullAction : &printDeclsEntryAction));
  }

  if (!faux_decls) {
    fputs("\n", decls_fp);
  }

  if (kvasir_with_dyncomp) {
    if (faux_decls) {
      // Allocate program point data structures if we are using DynComp:
      // (This should only be run once for every ppt)
      // This must be run at the end because its results depend on
      // g_variableIndex being properly incremented
      allocate_ppt_structures((DaikonFunctionEntry*)funcPtr, isEnter, g_variableIndex);
    }
    else {
      genfreehashtable(g_compNumberMap);
    }
  }

}


// Print out all function declarations in Daikon .decls format
static void printAllFunctionDecls(char faux_decls)
{
  FuncIterator* funcIt = newFuncIterator();

  while (hasNextFunc(funcIt)) {
    FunctionEntry* cur_entry = nextFunc(funcIt);

    tl_assert(cur_entry);

    // If fjalar_trace_prog_pts_filename is OFF, then ALWAYS
    // print out all program point .decls
    if (!fjalar_trace_prog_pts_filename ||
        // If fjalar_trace_prog_pts_filename is on (we are reading in
        // a ppt list file), then DO NOT OUTPUT .decls entries for
        // program points that we are not interested in tracing.  This
        // decreases the clutter of the .decls file and speeds up
        // processing time
        prog_pts_tree_entry_found(cur_entry)) {
      printOneFunctionDecl(cur_entry, 1, faux_decls);
      printOneFunctionDecl(cur_entry, 0, faux_decls);
    }
  }

  deleteFuncIterator(funcIt);
}


// For C++ only: Print out an :::OBJECT program point.
// The object program point should consist of class_name:::OBJECT
// and all information from 'this'

// DynComp: Right now, do NOT try to print out comparability
// information for OBJECT program points.  We may support this in the
// future if necessary.
static void printAllObjectPPTDecls(void) {
  TypeIterator* typeIt = newTypeIterator();

  Bool hacked_dyncomp_switch = False;

  extern void stringStackPush(char** stringStack, int* pStringStackSize, char* str);
  extern char* stringStackPop(char** stringStack, int* pStringStackSize);

  extern char* fullNameStack[];
  extern int fullNameStackSize;


  // HACK ALERT: We need to temporarily pretend that we are not using
  // kvasir_with_dyncomp in order to print out the OBJECT program
  // points normally.  We need to set this back at the end of the
  // function:
  if (kvasir_with_dyncomp) {
    kvasir_with_dyncomp = False;
    hacked_dyncomp_switch = True;
  }

  while (hasNextType(typeIt)) {
    TypeEntry* cur_type = nextType(typeIt);
    tl_assert(cur_type);

    if (IS_AGGREGATE_TYPE(cur_type)) {

      // Only print out .decls for :::OBJECT program points if there
      // is at least 1 member function.  Otherwise, don't bother
      // because object program points will never be printed out for
      // this class in the .dtrace file.  Also, only print it out if
      // there is at least 1 member variable, or else there is no
      // point.
      if ((cur_type->aggType->memberFunctionList && (cur_type->aggType->memberFunctionList->numElts > 0)) &&
          (cur_type->aggType->memberVarList && (cur_type->aggType->memberVarList->numVars > 0)) &&
          cur_type->typeName) {
        tl_assert(cur_type->typeName);

        if (kvasir_new_decls_format) {
          // Example output:
          //   ppt Stack
          //   ppt-type object
          fputs("ppt ", decls_fp);
          fputs(cur_type->typeName, decls_fp);
          fputs("\n  ppt-type object\n", decls_fp);
        }
        else {
          fputs("DECLARE\n", decls_fp);
          fputs(cur_type->typeName, decls_fp);
          fputs(":::OBJECT\n", decls_fp);
        }

        stringStackPush(fullNameStack, &fullNameStackSize, "this");
        stringStackPush(fullNameStack, &fullNameStackSize, ARROW);

        // Print out regular member vars.
        visitClassMembersNoValues(cur_type, &printDeclsEntryAction);

        stringStackPop(fullNameStack, &fullNameStackSize);
        stringStackPop(fullNameStack, &fullNameStackSize);

        fputs("\n", decls_fp);

        // TODO: What do we do about static member vars?
        // Right now we just print them out like globals
      }
    }
  }

  deleteTypeIterator(typeIt);

  // HACK ALERT! Remember to restore original state
  if (hacked_dyncomp_switch) {
    kvasir_with_dyncomp = True;
  }
}
