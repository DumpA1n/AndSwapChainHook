#!/usr/bin/env python3
"""Build and optionally execute the owned SurfaceView contract fixture."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import time
import zipfile

parser=argparse.ArgumentParser()
parser.add_argument('--device', help='Explicit adb serial; omit to build only')
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
ndk=Path(os.environ['NDK_HOME'])
sdk=Path(os.environ['ANDROID_SDK_ROOT'])
java=Path(os.environ['JAVA_HOME'])/'bin'
tools=sdk/'build-tools/34.0.0'
android=sdk/'platforms/android-34/android.jar'
build=root/'build/overlay-fixture'
work=build/'apk'
work.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,PATH=str(java)+os.pathsep+os.environ['PATH'])
def run(command,**kwargs):
    return subprocess.run([str(item) for item in command],check=True,env=env,**kwargs)
run(['cmake','-S',root,'-B',build,'-G','Ninja',f'-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake',
     '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-27','-DANDROID_STL=c++_static',
     '-DCMAKE_BUILD_TYPE=Release','-DANDSWAPCHAIN_BUILD_OVERLAY=ON','-DBUILD_TESTING=ON'])
run(['cmake','--build',build,'--target','andswapchain_overlay_fixture'])
for directory in ['classes','dex']:
    target=work/directory
    if target.exists():shutil.rmtree(target)
    target.mkdir()
app=root/'tests/overlay/app'
run([java/'javac','--release','11','-classpath',android,'-d',work/'classes',*app.rglob('*.java')])
run([java/'jar','--create','--file',work/'classes.jar','-C',work/'classes','.'])
run([tools/'d8','--release','--min-api','27','--lib',android,'--output',work/'dex',work/'classes.jar'])
run([tools/'aapt2','link','-I',android,'--manifest',app/'AndroidManifest.xml','-o',work/'base.apk'])
with zipfile.ZipFile(work/'base.apk','a',zipfile.ZIP_DEFLATED) as archive:
    archive.write(work/'dex/classes.dex','classes.dex')
    archive.write(build/'libandswapchain_overlay_fixture.so','lib/arm64-v8a/libandswapchain_overlay_fixture.so')
run([tools/'zipalign','-f','-p','4',work/'base.apk',work/'aligned.apk'])
key=work/'fixture.keystore'
if not key.exists():
    run([java/'keytool','-genkeypair','-keystore',key,'-alias','fixture','-keyalg','RSA','-keysize','2048',
         '-validity','3650','-storepass','android','-keypass','android','-dname','CN=Owned SurfaceView Fixture'])
apk=work/'owned-overlay.apk'
run([tools/'apksigner','sign','--ks',key,'--ks-pass','pass:android','--key-pass','pass:android','--out',apk,work/'aligned.apk'])
run([tools/'apksigner','verify',apk])
print(apk,flush=True)
if args.device:
    adb=['adb','-s',args.device]
    package='com.andswapchain.owned'
    run([*adb,'install','-r','-t',apk])
    run([*adb,'shell','am','force-stop',package])
    run([*adb,'shell','run-as',package,'rm','-f','files/result.txt'])
    run([*adb,'shell','am','start','-W','-n',package+'/.MainActivity'])
    deadline=time.monotonic()+30
    result=''
    while time.monotonic()<deadline:
        output=subprocess.run([*adb,'shell','run-as',package,'cat','files/result.txt'],capture_output=True,text=True)
        result=output.stdout.strip()
        if result:break
        time.sleep(0.25)
    (work/'result.txt').write_text(result or 'UNAVAILABLE: owned app did not publish a result')
    print(result,flush=True)
    if not result.startswith('PASS '):raise SystemExit(1)
