#!/usr/bin/env python3
"""Verify the advertised component configurations and owned runtime contracts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--device',help='Explicit adb serial for owned native fixtures')
parser.add_argument('--pointerhook-prefix',type=Path,help='Validated installed PointerHook prefix')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
evidence=root/'build/evidence'
evidence.mkdir(parents=True,exist_ok=True)
checks=[]
def execute(name,command,cwd=root):
    result=subprocess.run([str(item) for item in command],cwd=cwd,env=os.environ.copy(),capture_output=True,text=True)
    (evidence/(name+'.log')).write_text(result.stdout+result.stderr)
    print(name+': '+('PASS' if result.returncode==0 else 'FAIL'),flush=True)
    checks.append({'name':name,'status':'passed' if result.returncode==0 else 'failed','command':['OWNED_DEVICE_SERIAL' if args.device and str(x)==args.device else str(x) for x in command],'exit_code':result.returncode})
    if result.returncode:raise SystemExit(result.returncode)
    return result.stdout
for preset in ['host-debug','host-release','host-optional','android-debug','android-release','android-modules-debug','android-modules-release']:
    options=[]
    if preset=='android-modules-release' and args.pointerhook_prefix:
        options=['-DANDSWAPCHAIN_BUILD_POINTER_ADAPTER=ON',f'-DPointerHook_DIR={args.pointerhook_prefix}/lib/cmake/PointerHook']
    execute(preset+'-configure',['cmake','--preset',preset,*options])
    execute(preset+'-build',['cmake','--build','--preset',preset])
    if preset.startswith('host-'):
        execute(preset+'-ctest',['ctest','--preset',preset,'--no-tests=error'])
execute('host-install',['cmake','--install',root/'build/host-release','--prefix',root/'build/install-host'])
execute('host-installed-consumer-configure',['cmake','-S',root/'tests/consumer','-B',root/'build/consumer-installed','-G','Ninja',f'-DCMAKE_PREFIX_PATH={root}/build/install-host'])
execute('host-installed-consumer-build',['cmake','--build',root/'build/consumer-installed'])
execute('host-installed-consumer-run',[root/'build/consumer-installed/consumer'])
execute('host-subdirectory-consumer-configure',['cmake','-S',root/'tests/consumer','-B',root/'build/consumer-subdir','-G','Ninja',f'-DANDSWAPCHAIN_SOURCE_DIR={root}'])
execute('host-subdirectory-consumer-build',['cmake','--build',root/'build/consumer-subdir'])
execute('host-subdirectory-consumer-run',[root/'build/consumer-subdir/consumer'])
execute('android-install',['cmake','--install',root/'build/android-modules-release','--prefix',root/'build/install-android'])
consumerOptions=[]
if args.pointerhook_prefix:consumerOptions=[f'-DPointerHook_DIR={args.pointerhook_prefix}/lib/cmake/PointerHook']
execute('android-installed-consumer-configure',['cmake','-S',root/'tests/consumer','-B',root/'build/consumer-android-installed','-G','Ninja',f'-DCMAKE_TOOLCHAIN_FILE={os.environ["NDK_HOME"]}/build/cmake/android.toolchain.cmake','-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-27',f'-DCMAKE_FIND_ROOT_PATH={root}/build/install-android','-DANDSWAPCHAIN_CONSUME_ANDROID_MODULES=ON',*consumerOptions])
execute('android-installed-consumer-build',['cmake','--build',root/'build/consumer-android-installed'])
androidToolchain=[f'-DCMAKE_TOOLCHAIN_FILE={os.environ["NDK_HOME"]}/build/cmake/android.toolchain.cmake','-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-27']
execute('dobby-repro-configure',['cmake','-S',root/'cmake/dobby','-B',root/'build/dobby-repro','-G','Ninja','-DCMAKE_BUILD_TYPE=Release',*androidToolchain])
execute('dobby-repro-build',['cmake','--build',root/'build/dobby-repro'])
dobbyArchive=root/'build/android-modules-release/cmake/dobby/libandswapchain_dobby.a'
reproArchive=root/'build/dobby-repro/libandswapchain_dobby.a'
if dobbyArchive.read_bytes()!=reproArchive.read_bytes():raise SystemExit('Dobby independent build archives differ')
checks.append({'name':'dobby-independent-build-byte-identity','status':'passed','exit_code':0})
llvm=Path(os.environ['NDK_HOME'])/'toolchains/llvm/prebuilt/darwin-x86_64/bin'
sections=execute('dobby-matching-symbols',[llvm/'llvm-readelf','--sections',dobbyArchive])
if '.debug_info' not in sections or '.symtab' not in sections:raise SystemExit('Dobby matching symbols are missing')
execute('dobby-external-target-consumer-configure',['cmake','-S',root/'tests/dobby_consumer','-B',root/'build/dobby-consumer','-G','Ninja',*androidToolchain,f'-DDobby_DIR={root}/build/install-android/lib/cmake/Dobby',f'-DANDSWAPCHAIN_SOURCE_DIR={root}'])
execute('dobby-external-target-consumer-build',['cmake','--build',root/'build/dobby-consumer'])
if args.device:
    adb=['adb','-s',args.device]
    remote='/data/local/tmp/andswapchain-refactor-owned'
    execute('device-fixture-directory',[*adb,'shell','mkdir','-p',remote])
    build=root/'build/android-modules-release'
    binaries=['andswapchain_input_tests','andswapchain_contract_tests','andswapchain_load_test','andswapchain_dobby_test','andswapchain_egl_test','andswapchain_crash_record_test','libAndSwapChainHook.so']
    if args.pointerhook_prefix:binaries.append('andswapchain_pointer_adapter_test')
    execute('device-fixture-copy',[*adb,'push',*[build/name for name in binaries],remote+'/'])
    for binary in binaries:
        if binary.endswith('.so'):continue
        argument=' ./libAndSwapChainHook.so' if binary=='andswapchain_load_test' else ''
        execute(binary+'-device',[*adb,'shell',f'cd {remote} && chmod 700 {binary} && timeout 30 ./{binary}{argument}'])
    execute('android-installed-consumer-copy',[*adb,'push',root/'build/consumer-android-installed/consumer',remote+'/consumer'])
    execute('android-installed-consumer-run',[*adb,'shell',f'cd {remote} && chmod 700 consumer && timeout 30 ./consumer'])
    execute('dobby-external-target-consumer-copy',[*adb,'push',root/'build/dobby-consumer/dobby_consumer',remote+'/dobby_consumer'])
    execute('dobby-external-target-consumer-run',[*adb,'shell',f'cd {remote} && chmod 700 dobby_consumer && timeout 30 ./dobby_consumer'])
    device={
        'model':subprocess.check_output([*adb,'shell','getprop','ro.product.model'],text=True).strip(),
        'api':subprocess.check_output([*adb,'shell','getprop','ro.build.version.sdk'],text=True).strip(),
        'build':subprocess.check_output([*adb,'shell','getprop','ro.build.display.id'],text=True).strip(),
        'page_size':int(subprocess.check_output([*adb,'shell','getconf','PAGESIZE'],text=True).strip()),
        'serial_sha256':hashlib.sha256(args.device.encode()).hexdigest()
    }
else:device=None
source_paths=[]
for directory in ['include','source/Memory','source/Analysis','source/Instrumentation','source/Android','source/Diagnostics','source/Input','tests']:
    source_paths += [path for path in (root/directory).rglob('*') if path.is_file() and path.name!='verification.json']
source_paths += [root/'source/Library.cpp',root/'source/Graphics/SwapChainHook.cpp',root/'CMakeLists.txt',root/'CMakePresets.json',*[path for path in sorted((root/'cmake').rglob('*')) if path.is_file()]]
artifacts=[dobbyArchive,reproArchive,root/'build/dobby-consumer/dobby_consumer']
for preset in ['host-debug','host-release','host-optional','android-debug','android-release','android-modules-debug','android-modules-release']:
    for path in (root/'build'/preset).iterdir():
        if path.is_file() and (path.suffix in ['.a','.so'] or path.name.startswith('andswapchain_') and not path.suffix):artifacts.append(path)
for path in [root/'build/overlay-fixture/apk/owned-overlay.apk',root/'build/overlay-fixture/libandswapchain_overlay_fixture.so']:
    if path.is_file():artifacts.append(path)
def digest(path):return {'path':str(path.relative_to(root)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
manifest={
    'schema':1,'baseline_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
    'source_state':'uncommitted implementation; source hashes are authoritative',
    'ndk_revision':'29.0.14206865','android_abi':'arm64-v8a','android_api':27,
    'checks':checks,'device':device,
    'dobby':{'repository':'https://github.com/jmpews/Dobby','revision':'5dfc8546954ce3b3198132ab13fddb89ee92cdd7','source_archive_sha256':'01a417233c40929aa21098c8a9724c00f6f05b5cde3f11abcac84b8e00053748','contract_version':1,'license':'Apache-2.0','options':['Android AArch64','C++17','PIC','logging disabled','near branches disabled','no constructor','no instruction instrumentation','debug symbols and frame pointers','path remapping'],'independent_build_archive_identity':True,'matching_symbols':'.debug_info and .symtab in the exact archived objects','system_egl_pac_entries':'unsupported; owned presentation entries call real EGL'},
    'traceability':['R22','R01','R05','R07','R08','R16','R18','R21','C02','C04','C05','C07','C09','E01','E03','E04','E07'],
    'limitations':['Dobby restoration requires external retirement of every acquired replacement/original address.','Vulkan host provider is unsupported; Vulkan synchronization and Android input-provider/ImGui queue integration remain open.','16 KiB, MTE, BTI/PAC, ShadowCallStack and general concurrent patching are unverified.','Legacy UI/auxiliary source and private downstream migration remain outside validated components.','System EGL exports with PAC/BTI entries are unsupported; owned entry fixtures call real system EGL.'],
    'source_hashes':[digest(path) for path in sorted(set(source_paths))],
    'artifacts':[digest(path) for path in sorted(artifacts)]
}
if args.pointerhook_prefix:
    manifest['pointerhook_package']={'revision':'uncommitted validated producer package; no gitlink changed','artifacts':[{'path':str(path.relative_to(args.pointerhook_prefix)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in sorted(args.pointerhook_prefix.rglob('*.a'))]}
result=root/'build/overlay-fixture/apk/result.txt'
if result.exists():manifest['owned_overlay_result']=result.read_text().strip()
(root/'tests/verification.json').write_text(json.dumps(manifest,indent=2)+'\n')
