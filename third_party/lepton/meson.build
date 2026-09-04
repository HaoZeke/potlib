# Vendored OpenMM Lepton. Compiled only when the parent enables with_expr.
lepton_inc = include_directories('include')
lepton_lib = static_library(
    'lepton',
    files(
        'src/CompiledExpression.cpp',
        'src/CompiledVectorExpression.cpp',
        'src/ExpressionProgram.cpp',
        'src/ExpressionTreeNode.cpp',
        'src/Operation.cpp',
        'src/ParsedExpression.cpp',
        'src/Parser.cpp',
    ),
    include_directories: lepton_inc,
    cpp_args: ['-DLEPTON_BUILDING_STATIC_LIBRARY'],
    pic: true,
    install: false,
)
lepton_dep = declare_dependency(
    link_with: lepton_lib,
    include_directories: lepton_inc,
    compile_args: ['-DLEPTON_USE_STATIC_LIBRARIES'],
)
