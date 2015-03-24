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
#include <memory>
using namespace llvm;

static cl::list<std::string>
InputFilenames(cl::Positional, cl::OneOrMore,
               cl::desc("<input bitcode files>"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"), cl::init("-"),
               cl::value_desc("filename"));

static cl::opt<bool>
Force("f", cl::desc("Enable binary output on terminals"));

static cl::opt<bool>
OutputAssembly("S",
         cl::desc("Write output as LLVM assembly"), cl::Hidden);

static cl::opt<bool>
Verbose("v", cl::desc("Print information about actions taken"));

static cl::opt<bool>
DumpAsm("d", cl::desc("Print assembly as linked"), cl::Hidden);

static cl::opt<bool>
SuppressWarnings("suppress-warnings", cl::desc("Suppress all linking warnings"),
                 cl::init(false));

static MDNode* LibraryMetadata = nullptr;

// Read the specified bitcode file in and return it. This routine searches the
// link path for the specified file to try to find it...
//
static std::unique_ptr<Module>
loadFile(const char *argv0, const std::string &FN, LLVMContext &Context) {
  SMDiagnostic Err;
  if (Verbose) errs() << "Loading '" << FN << "'\n";
  std::unique_ptr<Module> Result = getLazyIRFileModule(FN, Err, Context);
  if (!Result)
    Err.print(argv0, errs());

  return Result;
}

static void diagnosticHandler(const DiagnosticInfo &DI) {
  unsigned Severity = DI.getSeverity();
  switch (Severity) {
  case DS_Error:
    errs() << "ERROR: ";
    break;
  case DS_Warning:
    if (SuppressWarnings)
      return;
    errs() << "WARNING: ";
    break;
  case DS_Remark:
  case DS_Note:
    llvm_unreachable("Only expecting warnings and errors");
  }

  DiagnosticPrinterRawOStream DP(errs());
  DI.print(DP);
  errs() << '\n';
}

// Link together llvm.libs named metadata. This is an array of MDTuples each of
// the form !{"library_name.bc", !compilation_units} and represents a
// collection of the compilation units that were compiled together into the
// library IR file "library_name.bc".  If SrcM doesn't have an llvm.libs
// NamedMDNode, its compilation units are considered part of the library DstM.
void linkInLibraryMetadata(Module* SrcM, Module* DstM) {
  // If srcM doesn't have the named metadata node llvm.libs, then add srcM's
  // compilation units to DstM's list of direct compilation units, otherwise
  // normal link process will have merge the llvm.libs
  LLVMContext& Context = DstM->getContext();
  MDTuple* CUs;
  if (LibraryMetadata == NULL) {
    CUs = MDTuple::get(Context, ArrayRef<Metadata*>());
    SmallVector<Metadata*, 3> args;
    args.push_back(MDString::get(Context, DstM->getName()));
    args.push_back(CUs);
    LibraryMetadata = MDTuple::get(Context, args);
    if (NamedMDNode* NMD = DstM->getOrInsertNamedMetadata("llvm.libs")) {
      NMD->addOperand(LibraryMetadata);
    }
  }
  else {
    CUs = cast<MDTuple>(LibraryMetadata->getOperand(1).get());
  }

  if (!SrcM->getNamedMetadata("llvm.libs")) {
    // Add SrcM's compilation units to those in LibraryMetadata
    // take care to use the linked-in CUs to avoid duplicating
    // debug metadata nodes
    SmallVector<Metadata*, 16> SrcCUMDs;
    if (NamedMDNode* SrcCUs = SrcM->getNamedMetadata("llvm.dbg.cu")) {
      if (NamedMDNode* DstCUs = DstM->getNamedMetadata("llvm.dbg.cu")) {
        unsigned int linkedCUsStartingIdx = DstCUs->getNumOperands() - SrcCUs->getNumOperands();
        for (unsigned int i = linkedCUsStartingIdx; i < DstCUs->getNumOperands(); i++) {
          MDNode* CU = DstCUs->getOperand(i);
          SrcCUMDs.push_back(CU);
        }
        MDNode* joinedCUs = MDNode::concatenate(CUs, MDTuple::get(Context, SrcCUMDs));
        LibraryMetadata->replaceOperandWith(1, joinedCUs);
      }
    }
  }
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);

  LLVMContext &Context = getGlobalContext();
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  cl::ParseCommandLineOptions(argc, argv, "llvm linker\n");

  std::string ModuleID = "llvm-link";
  if (OutputFilename != "-") {
    ModuleID = sys::path::filename(OutputFilename);
  }
  auto Composite = make_unique<Module>(ModuleID, Context);
  Linker L(Composite.get(), diagnosticHandler);

  for (unsigned i = 0; i < InputFilenames.size(); ++i) {
    std::unique_ptr<Module> M = loadFile(argv[0], InputFilenames[i], Context);
    if (!M.get()) {
      errs() << argv[0] << ": error loading file '" <<InputFilenames[i]<< "'\n";
      return 1;
    }

    if (Verbose) errs() << "Linking in '" << InputFilenames[i] << "'\n";

    if (L.linkInModule(M.get()))
      return 1;

    linkInLibraryMetadata(M.get(), Composite.get());
  }

  if (DumpAsm) errs() << "Here's the assembly:\n" << *Composite;

  std::error_code EC;
  tool_output_file Out(OutputFilename, EC, sys::fs::F_None);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  if (verifyModule(*Composite)) {
    errs() << argv[0] << ": linked module is broken!\n";
    return 1;
  }

  if (Verbose) errs() << "Writing bitcode...\n";
  if (OutputAssembly) {
    Out.os() << *Composite;
  } else if (Force || !CheckBitcodeOutputToConsole(Out.os(), true))
    WriteBitcodeToFile(Composite.get(), Out.os());

  // Declare success.
  Out.keep();

  return 0;
}
