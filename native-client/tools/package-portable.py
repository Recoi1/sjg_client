"""Create an allowlisted package; game data and user files never enter it."""
from pathlib import Path
import hashlib,json,shutil,urllib.request,zipfile,time,subprocess,sys,os
root=Path(__file__).resolve().parents[1];build=root/'build-game'
report=json.loads((build/'portable-final-verification.json').read_text())
exe=build/'galaxy_portable.exe'
assert report['exit']==0 and report['gui_after_centering_exit']==0 and report['full_regressions']['exit']==0 and report['portable_sha256']==hashlib.sha256(exe.read_bytes()).hexdigest(), 'Verify the current build before packaging'
folder=root/'dist'/('Galaxy-PC-Builder-'+time.strftime('%Y%m%d-%H%M%S'))
folder.mkdir(parents=True,exist_ok=False)
shutil.copy2(exe,folder/'galaxy.exe')
dlls=('SDL3.dll','nod.dll','webgpu_dawn.dll','dxcompiler.dll','dxil.dll','libpng16.dll','libzlib1.dll')
for name in dlls:shutil.copy2(build/name,folder/name)
licenses=folder/'licenses';licenses.mkdir()
sources={'Petari':'Petari/LICENSE','Aurora':'aurora/LICENSE','SDL3':'build-platform/_deps/sdl3_prebuilt-src/licenses/SDL3/LICENSE.txt',
         'libpng':'build-platform/_deps/png-src/LICENSE','zlib':'build-platform/_deps/zlib-src/LICENSE.md',
         'zstd':'build-platform/_deps/zstd-src/LICENSE','xxHash':'build-platform/_deps/xxhash-src/LICENSE',
         'fmt':'build-platform/_deps/fmt-src/LICENSE','abseil':'build-platform/_deps/abseil-cpp-src/LICENSE',
         'FreeType':'build-platform/_deps/freetype-src/docs/FTL.TXT','ImGui':'build-platform/_deps/imgui-src/LICENSE.txt',
         'Tracy':'build-platform/_deps/tracy-src/LICENSE','Tracy-backtrace':'build-platform/_deps/tracy-src/public/libbacktrace/LICENSE'}
for name,path in sources.items():shutil.copyfile(root/path,licenses/(name+'.txt'))
external={'nod-MIT':'https://raw.githubusercontent.com/encounter/nod/v2.0.0-alpha.12/LICENSE-MIT',
          'nod-Apache':'https://raw.githubusercontent.com/encounter/nod/v2.0.0-alpha.12/LICENSE-APACHE',
          'Dawn':'https://raw.githubusercontent.com/google/dawn/main/LICENSE',
          'DirectXShaderCompiler':'https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/main/LICENSE.TXT'}
for name,url in external.items():
    with urllib.request.urlopen(url,timeout=30) as response:data=response.read(256*1024)
    assert data and b'License' in data or b'Copyright' in data
    (licenses/(name+'.txt')).write_bytes(data)
(folder/'README.txt').write_text('Galaxy PC ROM builder\n\nOpen galaxy.exe and drag your own supported disc image into the window.\nSupported: Super Mario Galaxy USA RMGE01 revision 0, ISO / WBFS / RVZ.\nThe executable prepares local game resources; no compiler is needed.\nKeep your original ROM at its imported location.\n\nShare this clean ZIP. Never redistribute the game-data, userdata or mods folders\ncreated after importing. No ROM, extracted game resources, saves, HD videos,\ntexture packs or character mods are included here.\n\nWith an active Radmin adapter, Multiplayer appears above Back on the save-file\nconfirmation screen. Its native LAN browser offers Host and Join; four-player\nlobbies show independent character choices and disable local save writes.\nAll players need the same executable and character mods. This clean package\noffers Mario and Luigi; custom character assets must be installed separately.\n\nFour-player co-op gameplay is still being implemented. The lobby cannot start\na shared level yet. Separate-computer Radmin connectivity is not verified.\n',encoding='utf-8')
# Check that the original ROM-derived boot payloads are absent, including samples to catch accidental partial embedding.
binary=(folder/'galaxy.exe').read_bytes()
for name in ('ErrorMessageArchive.arc','GalaxyIDBCSV.bcsv','StoryEventBCSV.bcsv'):
    payload=(root/'build-assets/Memory'/name).read_bytes()
    assert payload not in binary,name
    for offset in range(0,len(payload)-64,256):
        sample=payload[offset:offset+64]
        if len(set(sample))>12:assert sample not in binary,(name,offset)
allowed={'galaxy.exe','README.txt',*dlls}
files=[p for p in folder.rglob('*') if p.is_file()]
assert all(p.parent==licenses or p.name in allowed and p.parent==folder for p in files)
manifest={str(p.relative_to(folder)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
(folder/'manifest.json').write_text(json.dumps({'files':manifest,'rom_assets_included':False,'lan_lobby_available':True,'multiplayer_playable':False},indent=2)+'\n')
archive=folder.with_suffix('.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for path in folder.rglob('*'):
        if path.is_file():z.write(path,folder.name+'/'+str(path.relative_to(folder)))
assert all(not any(part.lower() in ('game-data','userdata','mods','orig','files','movieData'.lower()) for part in Path(n).parts) for n in zipfile.ZipFile(archive).namelist())
(build/'portable-package.json').write_text(json.dumps({'folder':str(folder),'zip':str(archive),'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'boot_assets_absent':True,'lan_lobby_available':True,'multiplayer_playable':False},indent=2)+'\n')
print(archive)
publishing=root/'github-publishing.json'
if publishing.exists() and json.loads(publishing.read_text()).get('enabled') and os.environ.get('GALAXY_SKIP_PUBLISH')!='1':
    subprocess.run([sys.executable,str(root/'tools/publish-update.py')],check=True)
