{
  "name": "compilerpp",
  "groups": {
    "Sources": [
      "AST.cpp",
      "AST1.cpp",
      "Bytecode.cpp",
      "CodeGen.cpp",
      "Diagnostics.cpp",
      "IR.cpp",
      "Layout.cpp",
      "Lexer.cpp",
      "Lower.cpp",
      "Lower1.cpp",
      "Parser.cpp",
      "Parser1.cpp",
      "Semantic.cpp",
      "SymbolTable.cpp",
      "VM.cpp",
      "main.cpp"
    ],
    "Headers": [
      "AST.h",
      "AST1.h",
      "Bytecode.h",
      "CodeGen.h",
      "Diagnostics.h",
      "IR.h",
      "Layout.h",
      "Lexer.h",
      "Lower.h",
      "Lower1.h",
      "Parser.h",
      "Parser1.h",
      "Semantic.h",
      "SymbolTable.h",
      "Token.h",
      "VM.h"
    ],
    "Input": [
      "sample.cpp"
    ]
  },
  "build": {
    "target": "compilerpp",
    "groups": [
      "Sources"
    ],
    "args": [
      "-run",
      "sample.cpp"
    ]
  }
}
