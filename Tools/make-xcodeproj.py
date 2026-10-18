#!/usr/bin/env python3
# =============================================================================
#  make-xcodeproj.py - writes GRIDEm.xcodeproj from the folder layout the
#  Makefile builds from, so Xcode and `make` build the same files:
#
#    libGRIDEm    static library   Sources/{Core,Platform,Settings,Model,Services}/*.cpp
#                                  + Sources/Platform/*.mm
#    GRIDEm       the app          Sources/UI/**/*.mm, Resources/Info.plist, the resources
#    GRIDEmTests  XCTest bundle    Tests/*.mm, linking libGRIDEm only
#
#  plus two shared schemes, GRIDEm (build, run, test) and GRIDEmTests (`make test`).
#  The object ids are hashes of what they stand for, so running it again over the
#  same files writes the same project. Run it again after adding or removing a file:
#    python3 Tools/make-xcodeproj.py
#  The contract's language rule holds here as in the Makefile:
#  -std=c++14 -pedantic, warnings are errors.
# =============================================================================
import hashlib, os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
PROJ = os.path.join(ROOT, 'GRIDEm.xcodeproj')
LIB_LAYERS = ['Core', 'Platform', 'Settings', 'Model', 'Services']
FRAMEWORKS = ['AppKit', 'CoreServices', 'WebKit', 'UniformTypeIdentifiers']
RESOURCE_FILES = ['Resources/toolchain.json', 'Resources/Credits.rtf', 'Resources/GRIDEm.icns']
RESOURCE_FOLDERS = ['Resources/lib', 'Resources/docs', 'Resources/samples']

def oid(*parts):
    return hashlib.md5('/'.join(parts).encode('utf-8')).hexdigest()[:24].upper()

def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, '/')

def files_in(folder, exts, recursive=False):
    out = []
    base = os.path.join(ROOT, folder)
    if not os.path.isdir(base): return out
    if recursive:
        for d, _, names in os.walk(base):
            for n in names:
                if os.path.splitext(n)[1] in exts: out.append(rel(os.path.join(d, n)))
    else:
        for n in os.listdir(base):
            if os.path.splitext(n)[1] in exts and os.path.isfile(os.path.join(base, n)):
                out.append(rel(os.path.join(base, n)))
    return sorted(out)

FILE_TYPES = {
    '.cpp': 'sourcecode.cpp.cpp', '.h': 'sourcecode.c.h', '.mm': 'sourcecode.cpp.objcpp',
    '.plist': 'text.plist.xml', '.json': 'text.json', '.rtf': 'text.rtf', '.icns': 'image.icns',
    '.md': 'net.daringfireball.markdown', '.py': 'text.script.python',
}

# ---- an old-style (ASCII) property list, as project.pbxproj is written ---------------
SAFE = re.compile(r'^[A-Za-z0-9_$/:.\-]+$')

def q(s):
    s = str(s)
    if SAFE.match(s) and not s.startswith('//'): return s
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'

def write_value(v, indent):
    pad = '\t' * indent
    if isinstance(v, dict):
        lines = ['{']
        for k in v:
            lines.append('%s\t%s = %s;' % (pad, q(k), write_value(v[k], indent + 1)))
        lines.append(pad + '}')
        return '\n'.join(lines)
    if isinstance(v, list):
        if not v: return '(\n%s)' % pad
        return '(\n' + ''.join('%s\t%s,\n' % (pad, write_value(i, indent + 1)) for i in v) + pad + ')'
    return q(v)

# ---- collect --------------------------------------------------------------------------
lib_sources = []
lib_headers = []
for layer in LIB_LAYERS:
    lib_sources += files_in('Sources/' + layer, ['.cpp'])
    lib_headers += files_in('Sources/' + layer, ['.h'])
lib_sources += files_in('Sources/Platform', ['.mm'])
ui_sources = files_in('Sources/UI', ['.mm'], recursive=True)
ui_headers = files_in('Sources/UI', ['.h'], recursive=True)
test_sources = files_in('Tests', ['.mm'])
test_headers = files_in('Tests', ['.h'])
resources = [r for r in RESOURCE_FILES if os.path.isfile(os.path.join(ROOT, r))]
folders = [r for r in RESOURCE_FOLDERS if os.path.isdir(os.path.join(ROOT, r))]
plist = 'Resources/Info.plist'
others = [p for p in ['Makefile', 'README.md', 'docs/PORTING.md', 'Tools/make-xcodeproj.py']
          if os.path.isfile(os.path.join(ROOT, p))]

objects = {}

def add(key, obj):
    objects[key] = obj
    return key

# file references, by path
fileref = {}
for path in sorted(set(lib_sources + lib_headers + ui_sources + ui_headers + test_sources + test_headers
                       + resources + [plist] + others)):
    ext = os.path.splitext(path)[1]
    fileref[path] = add(oid('file', path), {
        'isa': 'PBXFileReference', 'lastKnownFileType': FILE_TYPES.get(ext, 'text'),
        'path': os.path.basename(path), 'sourceTree': '<group>'})
for path in folders:
    fileref[path] = add(oid('folder', path), {
        'isa': 'PBXFileReference', 'lastKnownFileType': 'folder',
        'path': os.path.basename(path), 'sourceTree': '<group>'})
for fw in FRAMEWORKS:
    fileref['fw:' + fw] = add(oid('framework', fw), {
        'isa': 'PBXFileReference', 'lastKnownFileType': 'wrapper.framework', 'name': fw + '.framework',
        'path': 'System/Library/Frameworks/%s.framework' % fw, 'sourceTree': 'SDKROOT'})
products = {
    'lib': add(oid('product', 'lib'), {'isa': 'PBXFileReference', 'explicitFileType': 'archive.ar',
                                       'includeInIndex': '0', 'path': 'libGRIDEm.a', 'sourceTree': 'BUILT_PRODUCTS_DIR'}),
    'app': add(oid('product', 'app'), {'isa': 'PBXFileReference', 'explicitFileType': 'wrapper.application',
                                       'includeInIndex': '0', 'path': 'GRIDEm.app', 'sourceTree': 'BUILT_PRODUCTS_DIR'}),
    'tests': add(oid('product', 'tests'), {'isa': 'PBXFileReference', 'explicitFileType': 'wrapper.cfbundle',
                                           'includeInIndex': '0', 'path': 'GRIDEmTests.xctest', 'sourceTree': 'BUILT_PRODUCTS_DIR'}),
}

# groups mirroring the folders
groups = {}
top_children = []

def group_for(folder):
    if folder in groups: return groups[folder]
    key = add(oid('group', folder), {'isa': 'PBXGroup', 'children': [], 'path': os.path.basename(folder),
                                     'sourceTree': '<group>'})
    groups[folder] = key
    parent = os.path.dirname(folder)
    if parent: objects[group_for(parent)]['children'].append(key)
    else: top_children.append(key)
    return key

for path in sorted(fileref):
    if path.startswith('fw:'): continue
    folder = os.path.dirname(path)
    if folder: objects[group_for(folder)]['children'].append(fileref[path])
    else: top_children.append(fileref[path])
fw_group = add(oid('group', 'Frameworks'), {'isa': 'PBXGroup', 'name': 'Frameworks', 'sourceTree': '<group>',
                                            'children': [fileref['fw:' + f] for f in FRAMEWORKS]})
prod_group = add(oid('group', 'Products'), {'isa': 'PBXGroup', 'name': 'Products', 'sourceTree': '<group>',
                                            'children': [products['lib'], products['app'], products['tests']]})
for key in groups.values():
    objects[key]['children'].sort(key=lambda k: (objects[k]['isa'] != 'PBXGroup', objects[k].get('path', '')))
main_group = add(oid('group', 'main'), {'isa': 'PBXGroup', 'sourceTree': '<group>',
                                        'children': sorted(top_children, key=lambda k: objects[k].get('path', ''))
                                                    + [fw_group, prod_group]})

def build_files(target, paths, phase):
    keys = []
    for p in paths:
        keys.append(add(oid('buildfile', target, phase, p), {'isa': 'PBXBuildFile', 'fileRef': fileref[p]}))
    return keys

def phase(target, kind, files):
    return add(oid('phase', target, kind), {'isa': kind, 'buildActionMask': '2147483647', 'files': files,
                                            'runOnlyForDeploymentPostprocessing': '0'})

# the toolchain inside the app, as `make app` puts it there: Tools/build-toolchain.sh's programs into
# Contents/MacOS (each signed ad hoc, so the app's own signature can be made), its runtimes into
# Contents/Resources/lib, and Contents/lib for shalimar, which looks in ../lib beside itself
TOOLCHAIN_SCRIPT = """TC="${SRCROOT}/build/toolchain"
if [ ! -d "$TC/bin" ]; then
  echo "warning: no toolchain in $TC - run Tools/build-toolchain.sh first; GRIDEm will not find cpp11, c90 ..."
  exit 0
fi
MACOS="${TARGET_BUILD_DIR}/${EXECUTABLE_FOLDER_PATH}"
RES="${TARGET_BUILD_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}"
mkdir -p "$MACOS" "$RES/lib"
for f in "$TC"/bin/*; do
  if [ -f "$f" ] && [ -x "$f" ]; then
    cp -f "$f" "$MACOS/"
    codesign --force --sign - "$MACOS/$(basename "$f")" >/dev/null 2>&1 || true
  fi
done
if [ -d "$TC/lib" ]; then cp -R "$TC/lib/." "$RES/lib/"; fi
CONTENTS="$(dirname "$MACOS")"
if [ ! -e "$CONTENTS/lib" ]; then ln -s Resources/lib "$CONTENTS/lib"; fi
"""

def script_phase(target, name, body):
    return add(oid('phase', target, 'script', name), {
        'isa': 'PBXShellScriptBuildPhase', 'buildActionMask': '2147483647', 'files': [],
        'inputPaths': [], 'outputPaths': [], 'name': name, 'alwaysOutOfDate': '1',
        'runOnlyForDeploymentPostprocessing': '0', 'shellPath': '/bin/sh', 'shellScript': body,
        'showEnvVarsInLog': '0'})

# ---- build settings ----------------------------------------------------------------------
COMMON = {
    'ALWAYS_SEARCH_USER_PATHS': 'NO',
    'CLANG_CXX_LANGUAGE_STANDARD': 'c++14',
    'CLANG_CXX_LIBRARY': 'libc++',
    'CLANG_ENABLE_OBJC_ARC': 'YES',
    'CLANG_ENABLE_MODULES': 'YES',
    'GCC_TREAT_WARNINGS_AS_ERRORS': 'YES',
    'OTHER_CPLUSPLUSFLAGS': ['$(OTHER_CFLAGS)', '-pedantic'],
    'WARNING_CFLAGS': ['-Wall', '-Wextra'],
    'HEADER_SEARCH_PATHS': ['$(SRCROOT)/Sources'],
    'MACOSX_DEPLOYMENT_TARGET': '13.0',
    'SDKROOT': 'macosx',
    'CODE_SIGN_IDENTITY': '-',
    'CODE_SIGN_STYLE': 'Manual',
}

def configs(owner, debug_extra, release_extra):
    lst = []
    for name, extra in (('Debug', debug_extra), ('Release', release_extra)):
        lst.append(add(oid('config', owner, name), {'isa': 'XCBuildConfiguration', 'name': name,
                                                     'buildSettings': extra}))
    return add(oid('configlist', owner), {'isa': 'XCConfigurationList', 'buildConfigurations': lst,
                                          'defaultConfigurationIsVisible': '0',
                                          'defaultConfigurationName': 'Release'})

project_debug = dict(COMMON, **{'DEBUG_INFORMATION_FORMAT': 'dwarf', 'GCC_OPTIMIZATION_LEVEL': '0',
                                'ONLY_ACTIVE_ARCH': 'YES', 'ENABLE_TESTABILITY': 'YES',
                                'GCC_PREPROCESSOR_DEFINITIONS': ['DEBUG=1', '$(inherited)']})
project_release = dict(COMMON, **{'DEBUG_INFORMATION_FORMAT': 'dwarf-with-dsym', 'GCC_OPTIMIZATION_LEVEL': 's'})

lib_settings = {'PRODUCT_NAME': 'GRIDEm', 'EXECUTABLE_PREFIX': 'lib', 'SKIP_INSTALL': 'YES'}
app_settings = {'PRODUCT_NAME': 'GRIDEm', 'ENABLE_USER_SCRIPT_SANDBOXING': 'NO', 'INFOPLIST_FILE': plist,
                'PRODUCT_BUNDLE_IDENTIFIER': 'com.akhtar.GRIDEm', 'COMBINE_HIDPI_IMAGES': 'YES',
                'LD_RUNPATH_SEARCH_PATHS': ['$(inherited)', '@executable_path/../Frameworks']}
test_settings = {'PRODUCT_NAME': 'GRIDEmTests', 'PRODUCT_BUNDLE_IDENTIFIER': 'com.akhtar.GRIDEmTests',
                 'GENERATE_INFOPLIST_FILE': 'YES',
                 # XCTest's own macros are not -pedantic clean: XCTAssertTrue(x) with no message passes
                 # nothing to the '...' (a C++20 extension), and XCTAssertEqual and its kin are GNU
                 # statement expressions. The warnings come from XCTest, not from GRIDEm's code
                 'WARNING_CFLAGS': ['$(inherited)', '-Wno-c++20-extensions', '-Wno-gnu-zero-variadic-macro-arguments',
                                    '-Wno-gnu-statement-expression', '-Wno-gnu-statement-expression-from-macro-expansion'],
                 'GCC_PREPROCESSOR_DEFINITIONS': ['$(inherited)', 'GRIDEM_SOURCE_ROOT=\\"$(SRCROOT)\\"'],
                 'LD_RUNPATH_SEARCH_PATHS': ['$(inherited)', '@loader_path/../Frameworks']}

# ---- targets -------------------------------------------------------------------------------
project_key = oid('project')

def proxy_dependency(owner, target_key, name):
    proxy = add(oid('proxy', owner, name), {'isa': 'PBXContainerItemProxy', 'containerPortal': project_key,
                                            'proxyType': '1', 'remoteGlobalIDString': target_key,
                                            'remoteInfo': name})
    return add(oid('dependency', owner, name), {'isa': 'PBXTargetDependency', 'target': target_key,
                                                'targetProxy': proxy})

lib_key = oid('target', 'libGRIDEm')
app_key = oid('target', 'GRIDEm')
tests_key = oid('target', 'GRIDEmTests')

lib_link = add(oid('buildfile', 'link-lib'), {'isa': 'PBXBuildFile', 'fileRef': products['lib']})
lib_link_tests = add(oid('buildfile', 'link-lib-tests'), {'isa': 'PBXBuildFile', 'fileRef': products['lib']})

objects[lib_key] = {
    'isa': 'PBXNativeTarget', 'name': 'libGRIDEm', 'productName': 'GRIDEm',
    'productReference': products['lib'], 'productType': 'com.apple.product-type.library.static',
    'buildConfigurationList': configs('libGRIDEm', lib_settings, lib_settings),
    'buildPhases': [phase('lib', 'PBXSourcesBuildPhase', build_files('lib', lib_sources, 'src')),
                    phase('lib', 'PBXFrameworksBuildPhase', [])],
    'buildRules': [], 'dependencies': []}

app_frameworks = [lib_link] + build_files('app', ['fw:' + f for f in FRAMEWORKS], 'fw')
objects[app_key] = {
    'isa': 'PBXNativeTarget', 'name': 'GRIDEm', 'productName': 'GRIDEm',
    'productReference': products['app'], 'productType': 'com.apple.product-type.application',
    'buildConfigurationList': configs('GRIDEm', app_settings, app_settings),
    'buildPhases': [phase('app', 'PBXSourcesBuildPhase', build_files('app', ui_sources, 'src')),
                    phase('app', 'PBXFrameworksBuildPhase', app_frameworks),
                    phase('app', 'PBXResourcesBuildPhase', build_files('app', resources + folders, 'res')),
                    script_phase('app', 'Copy the toolchain into the app', TOOLCHAIN_SCRIPT)],
    'buildRules': [], 'dependencies': [proxy_dependency('GRIDEm', lib_key, 'libGRIDEm')]}

test_frameworks = [lib_link_tests] + build_files('tests', ['fw:CoreServices'], 'fw')
objects[tests_key] = {
    'isa': 'PBXNativeTarget', 'name': 'GRIDEmTests', 'productName': 'GRIDEmTests',
    'productReference': products['tests'], 'productType': 'com.apple.product-type.bundle.unit-test',
    'buildConfigurationList': configs('GRIDEmTests', test_settings, test_settings),
    'buildPhases': [phase('tests', 'PBXSourcesBuildPhase', build_files('tests', test_sources, 'src')),
                    phase('tests', 'PBXFrameworksBuildPhase', test_frameworks)],
    'buildRules': [], 'dependencies': [proxy_dependency('GRIDEmTests', lib_key, 'libGRIDEm')]}

objects[project_key] = {
    'isa': 'PBXProject',
    'attributes': {'BuildIndependentTargetsInParallel': '1', 'LastUpgradeCheck': '1600',
                   'ORGANIZATIONNAME': 'G. R. Akhtar'},
    'buildConfigurationList': configs('project', project_debug, project_release),
    'compatibilityVersion': 'Xcode 14.0', 'developmentRegion': 'en', 'hasScannedForEncodings': '0',
    'knownRegions': ['en', 'Base'], 'mainGroup': main_group, 'productRefGroup': prod_group,
    'projectDirPath': '', 'projectRoot': '', 'targets': [lib_key, app_key, tests_key]}

# ---- write project.pbxproj -------------------------------------------------------------------
ISA_ORDER = ['PBXBuildFile', 'PBXContainerItemProxy', 'PBXFileReference', 'PBXFrameworksBuildPhase',
             'PBXGroup', 'PBXNativeTarget', 'PBXProject', 'PBXResourcesBuildPhase', 'PBXShellScriptBuildPhase', 'PBXSourcesBuildPhase',
             'PBXTargetDependency', 'XCBuildConfiguration', 'XCConfigurationList']
out = ['// !$*UTF8*$!', '{', '\tarchiveVersion = 1;', '\tclasses = {', '\t};', '\tobjectVersion = 56;',
       '\tobjects = {']
for isa in ISA_ORDER:
    keys = sorted(k for k, o in objects.items() if o['isa'] == isa)
    if not keys: continue
    out.append('')
    out.append('/* Begin %s section */' % isa)
    for k in keys:
        body = dict(objects[k])
        ordered = {'isa': body.pop('isa')}
        for kk in sorted(body): ordered[kk] = body[kk]
        out.append('\t\t%s = %s;' % (k, write_value(ordered, 2)))
    out.append('/* End %s section */' % isa)
out += ['\t};', '\trootObject = %s;' % project_key, '}', '']

os.makedirs(os.path.join(PROJ, 'xcshareddata', 'xcschemes'), exist_ok=True)
with open(os.path.join(PROJ, 'project.pbxproj'), 'w', encoding='utf-8') as f:
    f.write('\n'.join(out))

# ---- the shared schemes ------------------------------------------------------------------------
def buildable(key, name, product):
    return ('<BuildableReference\n BuildableIdentifier = "primary"\n BlueprintIdentifier = "%s"\n'
            ' BuildableName = "%s"\n BlueprintName = "%s"\n ReferencedContainer = "container:GRIDEm.xcodeproj">\n'
            '</BuildableReference>' % (key, product, name))

def scheme(build_targets, test, launch):
    entries = ''.join(
        '<BuildActionEntry\n buildForTesting = "YES"\n buildForRunning = "%s"\n buildForProfiling = "YES"\n'
        ' buildForArchiving = "YES"\n buildForAnalyzing = "YES">\n%s\n</BuildActionEntry>\n'
        % ('YES' if run else 'NO', buildable(k, n, p)) for k, n, p, run in build_targets)
    launch_xml = ''
    if launch:
        launch_xml = ('<BuildableProductRunnable\n runnableDebuggingMode = "0">\n%s\n</BuildableProductRunnable>'
                      % buildable(*launch))
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<Scheme\n LastUpgradeVersion = "1600"\n version = "1.7">\n'
            '<BuildAction\n parallelizeBuildables = "YES"\n buildImplicitDependencies = "YES">\n'
            '<BuildActionEntries>\n' + entries + '</BuildActionEntries>\n</BuildAction>\n'
            '<TestAction\n buildConfiguration = "Debug"\n'
            ' selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB"\n'
            ' selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB"\n'
            ' shouldUseLaunchSchemeArgsEnv = "YES">\n'
            '<Testables>\n<TestableReference\n skipped = "NO">\n' + buildable(*test) + '\n</TestableReference>\n'
            '</Testables>\n</TestAction>\n'
            '<LaunchAction\n buildConfiguration = "Debug"\n'
            ' selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB"\n'
            ' selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB"\n'
            ' launchStyle = "0"\n useCustomWorkingDirectory = "NO"\n ignoresPersistentStateOnLaunch = "NO"\n'
            ' debugDocumentVersioning = "YES"\n debugServiceExtension = "internal"\n allowLocationSimulation = "YES">\n'
            + launch_xml + '\n</LaunchAction>\n'
            '<ProfileAction\n buildConfiguration = "Release"\n shouldUseLaunchSchemeArgsEnv = "YES"\n'
            ' savedToolIdentifier = ""\n useCustomWorkingDirectory = "NO"\n debugDocumentVersioning = "YES">\n'
            '</ProfileAction>\n'
            '<AnalyzeAction\n buildConfiguration = "Debug">\n</AnalyzeAction>\n'
            '<ArchiveAction\n buildConfiguration = "Release"\n revealArchiveInOrganizer = "YES">\n</ArchiveAction>\n'
            '</Scheme>\n')

app_ref = (app_key, 'GRIDEm', 'GRIDEm.app')
test_ref = (tests_key, 'GRIDEmTests', 'GRIDEmTests.xctest')
lib_ref = (lib_key, 'libGRIDEm', 'libGRIDEm.a')
with open(os.path.join(PROJ, 'xcshareddata', 'xcschemes', 'GRIDEm.xcscheme'), 'w', encoding='utf-8') as f:
    f.write(scheme([lib_ref + (False,), app_ref + (True,)], test_ref, app_ref))
with open(os.path.join(PROJ, 'xcshareddata', 'xcschemes', 'GRIDEmTests.xcscheme'), 'w', encoding='utf-8') as f:
    f.write(scheme([lib_ref + (False,), test_ref + (False,)], test_ref, None))

print('GRIDEm.xcodeproj: %d library sources, %d app sources, %d test sources, %d resources'
      % (len(lib_sources), len(ui_sources), len(test_sources), len(resources) + len(folders)))
