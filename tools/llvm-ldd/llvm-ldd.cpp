//===- llvm-link.cpp - Low-level LLVM linker ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This utility may be invoked in the following manner:
//  llvm-link a.bc b.bc c.bc -o x.bc
//
//===----------------------------------------------------------------------===//

#include "llvm/Linker/Linker.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/ADT/StringExtras.h"
#include <memory>
#include <unordered_map>
using namespace llvm;

static cl::list<std::string>
InputFilenames(cl::Positional, cl::OneOrMore,
               cl::desc("<input bitcode files>"));

static cl::opt<bool>
Verbose("v", cl::desc("Print information about actions taken"));

static cl::opt<bool>
Recursive("R", cl::desc("Print information recursively for all found libraries"));

static cl::opt<bool>
ListOnly("list-only", cl::desc("Print all required shared libs (one per line)"));

// Read the specified bitcode file in and return it. This routine searches the
// link path for the specified file to try to find it...
static std::unique_ptr<Module>
loadFile(const char *argv0, StringRef FN, LLVMContext &Context) {
  SMDiagnostic Err;
  if (Verbose) errs() << "Loading '" << FN << "'\n";
  std::unique_ptr<Module> Result = getLazyIRFileModule(FN, Err, Context);
  if (!Result)
    Err.print(argv0, errs());

  return Result;
}

static std::unordered_map<std::string, std::unique_ptr<Module>> LoadedModules;
static SmallVector<StringRef, 8> LibrarySearchPaths;

static raw_ostream& indented(int level) {
  for (int i = 0; i < level; i++) {
    outs() << "    ";
  }
  return outs();
}

static std::string findSharedLib(StringRef Name) {
  bool IsFullName = Name.rfind(".so.bc") != StringRef::npos || Name.rfind(".a.bc") != StringRef::npos;
  for (StringRef Path : LibrarySearchPaths) {
    // The metadata might contain a full name ("libQt5Core.so.bc.5.5.0") but it might also
    // only contain "libc" in which case we need to search for both libc.so.bc as well as
    // libc.a.bc since for our purposes these are both shared libraries.
    // We check .so.bc first since they include dependency information. The .a.bc
    // files lack this since it cannot be extracted from an ar command line.
    llvm::SmallString<128> Candidate;
    sys::path::append(Candidate, Path, Name);
    if (IsFullName) {
      if (Verbose) errs() << "Trying " << Candidate << "\n";
      if (sys::fs::is_regular_file(Candidate)) {
        return Candidate.str().str();
      }
    } else {
      // Why does Twine First = (Candidate.str() + ".so.bc"); is_regular_file(First) crash??
      if (Verbose) errs() << "Trying " << (Candidate.str() + ".so.bc") << "\n";
      if (sys::fs::is_regular_file(Candidate.str() + ".so.bc")) {
        return (Candidate.str() + ".so.bc").str();
      }
      if (Verbose) errs() << "Trying " << (Candidate.str() + ".a.bc") << "\n";
      if (sys::fs::is_regular_file(Candidate.str() + ".a.bc")) {
        return (Candidate.str() + ".a.bc").str();
      }
    }
  }
  return std::string();
}


int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);

  LLVMContext &Context = getGlobalContext();
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  cl::ParseCommandLineOptions(argc, argv, "llvm ldd\n");


  const char* LibPath = getenv("LLVM_IR_LIBRARY_PATH");
  if (LibPath) {
    StringRef(LibPath).trim().split(LibrarySearchPaths, ":", -1, false);
  }
  LibrarySearchPaths.push_back("/usr/local/lib");
  LibrarySearchPaths.push_back("/usr/lib");
  LibrarySearchPaths.push_back("/lib");
  if (Verbose) {
    errs() << "Library search path: ['" << join(LibrarySearchPaths.begin(), LibrarySearchPaths.end(), "', '") << "']\n";
  }

  bool error = false;
  for (unsigned i = 0; i < InputFilenames.size(); ++i) {
    StringRef InputFilename = InputFilenames[i];
    std::unique_ptr<Module> M = loadFile(argv[0], InputFilename, Context);
    if (!M.get()) {
      errs() << argv[0] << ": error loading file '" << InputFilename << "'\n";
      return 1;
    }

    if (!ListOnly) {
      outs() << InputFilename << ":\n";
    }

    // TODO: implement Recursive Flag
    if (NamedMDNode* NMD = M->getNamedMetadata("llvm.sharedlibs")) {
      if (NMD->getNumOperands() == 0) {
        continue; // there is no shared libs metadata
      } else if (NMD->getNumOperands() != 1) {
        errs() << "Invalid file format of " << InputFilename << "\n";
        if (Verbose) NMD->dump();
        error = true;
        continue;
      }
      MDNode* libs = NMD->getOperand(0);
      if (libs->getNumOperands() == 0) {
        if (!ListOnly) {
          indented(1) << "no shared libraries\n";
        }
        continue;
      }
      for (const MDOperand& lib : libs->operands()) {
        if (MDString* val = dyn_cast<MDString>(lib.get())) {
          StringRef Name = val->getString();
          if (Name.empty()) {
            errs() << "Invalid file format of " << InputFilename << ": Empty library name found!\n";
            error = true;
            continue;
          }
          if (ListOnly) {
            outs() << Name << '\n';
            continue;
          }
          std::string Result = findSharedLib(Name);
          if (Result.empty()) {
            Result = "not found";
          }
          indented(1) << Name << " => " << Result << "\n";
        } else {
          errs() << "Invalid file format of " << InputFilename << ": Operand is not a string!\n";
          if (Verbose) lib->dump();
        }
      }
    } else {
      if (!ListOnly) {
        indented(1) << "no shared libraries\n";
      }
    }
  }
  return error ? 1 : 0;
}
