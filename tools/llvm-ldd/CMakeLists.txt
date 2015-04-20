set(LLVM_LINK_COMPONENTS
  BitWriter
  Core
  IRReader
  Linker
  Support
  )

add_llvm_tool(llvm-ldd
  llvm-ldd.cpp
  )
