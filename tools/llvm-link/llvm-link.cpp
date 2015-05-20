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
#include "llvm/IR/AutoUpgrade.h"
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
#include <unordered_set>
using namespace llvm;

static cl::list<std::string>
InputFilenames(cl::Positional, cl::OneOrMore,
               cl::desc("<input bitcode files>"));

static cl::list<std::string> OverridingInputs(
    "override", cl::ZeroOrMore, cl::value_desc("filename"),
    cl::desc(
        "input bitcode file which can override previously defined symbol(s)"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"), cl::init("-"),
               cl::value_desc("filename"));

static cl::opt<bool>
Force("f", cl::desc("Enable binary output on terminals"));

static cl::list<std::string>
SharedLibraries("l", cl::Prefix, cl::desc("Shared libraries to be linked"), cl::value_desc("library"));

static cl::opt<bool>
InsertLibraryMetadata("libmd",
                      cl::desc("Insert library metadata"));

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

static cl::opt<bool> PreserveBitcodeUseListOrder(
    "preserve-bc-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM bitcode."),
    cl::init(true), cl::Hidden);

static cl::opt<bool> PreserveAssemblyUseListOrder(
    "preserve-ll-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM assembly."),
    cl::init(false), cl::Hidden);

// Read the specified bitcode file in and return it. This routine searches the
// link path for the specified file to try to find it...
//
static std::unique_ptr<Module>
loadFile(const char *argv0, StringRef FN, LLVMContext &Context) {
  SMDiagnostic Err;
  if (Verbose) errs() << "Loading '" << FN << "'\n";
  std::unique_ptr<Module> Result = getLazyIRFileModule(FN, Err, Context);
  if (!Result)
    Err.print(argv0, errs());

  Result->materializeMetadata();
  UpgradeDebugInfo(*Result);

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

static bool linkFiles(const char *argv0, LLVMContext &Context, Linker &L,
                      const cl::list<std::string> &Files,
                      bool OverrideDuplicateSymbols) {
  for (const auto &File : Files) {
    std::unique_ptr<Module> M = loadFile(argv0, File, Context);
    if (!M.get()) {
      errs() << argv0 << ": error loading file '" << File << "'\n";
      return false;
    }

    if (verifyModule(*M, &errs())) {
      errs() << argv0 << ": " << File << ": error: input module is broken!\n";
      return false;
    }

    if (Verbose)
      errs() << "Linking in '" << File << "'\n";

    if (L.linkInModule(M.get(), OverrideDuplicateSymbols))
      return false;
    
    if (InsertLibraryMetadata) {
      linkInLibraryMetadata(M.get(), L.getModule());
    }
  }

  return true;
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

  // First add all the regular input files
  if (!linkFiles(argv[0], Context, L, InputFilenames, false))
    return 1;

  // Next the -override ones.
  if (!linkFiles(argv[0], Context, L, OverridingInputs, true))
    return 1;

  if (DumpAsm) errs() << "Here's the assembly:\n" << *Composite;

  std::error_code EC;
  tool_output_file Out(OutputFilename, EC, sys::fs::F_None);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  // now add the shared library metadata (make sure we don't duplicate entries)
  std::unordered_set<std::string> SharedLibsSet;
  for (const std::string& Lib : SharedLibraries) {
    if (Verbose) {
      llvm::errs() << "Adding dependency on shared bitcode library lib"
                   << sys::path::filename(Lib) << '\n';
    }
    SharedLibsSet.insert("lib" + Lib);
  }
  // make sure the shared libs metadata is a flat list of strings
  NamedMDNode* NMD = Composite->getNamedMetadata("llvm.sharedlibs");
  if (NMD) {
    for (MDNode* Op : NMD->operands()) {
      for (const MDOperand& M : Op->operands()) {
        assert(isa<MDString>(M.get()));
        SharedLibsSet.insert(cast<MDString>(M.get())->getString().str());
      }
    }
    Composite->eraseNamedMetadata(NMD);
  }
  // TODO: remove those that were linked in
  // the following is not correct since we should also remove elements
  // when foo has "libc" in the metadata and we have libc.a.bc on the input command line
  for (const auto I : InputFilenames) {
    for (auto it = SharedLibsSet.begin(); it != SharedLibsSet.end(); ++it) {
      const std::string LibName = it->substr(0, it->find('.'));
      if (Verbose) errs() << "Base: " << LibName << " I:" << I << "\n";
      if (sys::path::filename(I).startswith(LibName + ".so.")
          || sys::path::filename(I).startswith(LibName + ".a.")) {
        if (Verbose) errs() << "Removing '" << *it << "'' from shared libs since '"
                            << I << "' is being linked in.\n";
        SharedLibsSet.erase(it);
        break;
      }
    }
  }
  llvm::SmallVector<Metadata*, 16> SharedLibsMD;
  for (const std::string& s : SharedLibsSet) {
    SharedLibsMD.push_back(MDString::get(Context, s));
  }
  if (!SharedLibsMD.empty()) {
    NMD = Composite->getOrInsertNamedMetadata("llvm.sharedlibs");
    assert(NMD);
    NMD->addOperand(MDTuple::get(Context, SharedLibsMD));
  }

  if (verifyModule(*Composite, &errs())) {
    errs() << argv[0] << ": error: linked module is broken!\n";
    return 1;
  }

  if (Verbose) errs() << "Writing bitcode...\n";
  if (OutputAssembly) {
    Composite->print(Out.os(), nullptr, PreserveAssemblyUseListOrder);
  } else if (Force || !CheckBitcodeOutputToConsole(Out.os(), true))
    WriteBitcodeToFile(Composite.get(), Out.os(), PreserveBitcodeUseListOrder);

  // Declare success.
  Out.keep();

  return 0;
}
