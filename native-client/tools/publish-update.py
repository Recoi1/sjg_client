"""Publish the verified, asset-free package and a source-only snapshot."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, os, re, shutil, subprocess, zipfile

ROOT=Path(__file__).resolve().parents[1]
DLLS={'SDL3.dll','nod.dll','webgpu_dawn.dll','dxcompiler.dll','dxil.dll','libpng16.dll','libzlib1.dll'}
REPOSITORY='Recoi1/sjg_client'

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def validate_package(path, expected, executable):
    if sha256(path)!=expected:raise ValueError('Package checksum changed')
    with zipfile.ZipFile(path) as archive:
        names=archive.namelist()
        if not names or len(names)!=len(set(names)):raise ValueError('Empty package or duplicate files')
        prefix=PurePosixPath(names[0]).parts[0]+'/'
        if any('\\' in name or not name.startswith(prefix) or '..' in PurePosixPath(name).parts for name in names):
            raise ValueError('Unsafe package path')
        manifest=json.loads(archive.read(prefix+'manifest.json'))
        if manifest.get('rom_assets_included') is not False:raise ValueError('Game assets cannot be published')
        files={name.replace('\\','/'):digest for name,digest in manifest['files'].items()}
        allowed={'galaxy.exe','README.txt',*DLLS}
        for name,digest in files.items():
            parts=PurePosixPath(name).parts
            if not (name in allowed or len(parts)==2 and parts[0]=='licenses' and parts[1].endswith('.txt')):
                raise ValueError('File outside package allowlist: '+name)
            if hashlib.sha256(archive.read(prefix+name)).hexdigest()!=digest:raise ValueError('Manifest checksum changed: '+name)
        if not allowed.issubset(files) or files['galaxy.exe']!=executable:raise ValueError('Wrong executable or missing runtime')
        if set(names)!={prefix+name for name in files}|{prefix+'manifest.json'}:raise ValueError('Undeclared ZIP contents')
        return manifest

def run(args,cwd=ROOT):
    return subprocess.run([str(arg) for arg in args],cwd=cwd,check=True,capture_output=True,text=True,encoding='utf-8',errors='replace').stdout.strip()

def export_sources(destination):
    selected=[ROOT/'CMakeLists.txt',ROOT/'MULTIPLAYER.md',ROOT/'AGENTS.md']
    for folder in ('native','tests'):
        selected.extend(p for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix in ('.cpp','.hpp','.c','.h','.py'))
    selected.extend(ROOT/'tools'/name for name in ('publish-update.py','package-portable.py','prepare-assets.py','fetch-sources.ps1',
                                                    'hd-texture-inventory.cpp','zig-cc.cmd','zig-cxx.cmd','zig-ar.cmd','zig-ranlib.cmd','zig-rc.cmd'))
    selected.append(ROOT/'build-game.ps1')
    public=destination/'native-client';public.mkdir(exist_ok=True)
    exported={}
    for source in sorted(selected):
        relative=source.relative_to(ROOT)
        data=source.read_text(encoding='utf-8-sig')
        if source.suffix=='.patch' and 'GIT binary patch' in data:raise ValueError('Binary source patch must be reviewed separately')
        if relative.as_posix()=='CMakeLists.txt':
            data=re.sub(r'set\(GALAXY_ROM_PATH "[^"]*" CACHE', 'set(GALAXY_ROM_PATH "" CACHE',data,count=1)
        target=public/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(data,encoding='utf-8',newline='\n')
        exported[relative.as_posix()]=sha256(target)
    for repo,name in [('Petari','Petari-native.patch'),('aurora','Aurora-mingw.patch')]:
        patch=run(['git','diff','--binary'],ROOT/repo)+'\n'
        untracked=run(['git','ls-files','--others','--exclude-standard'],ROOT/repo).splitlines()
        for filename in untracked:
            if Path(filename).suffix not in ('.cpp','.hpp','.c','.h','.cmake','.inc'):raise ValueError('Unreviewed upstream addition: '+filename)
            addition=subprocess.run(['git','diff','--no-index','--binary','--','/dev/null',filename],cwd=ROOT/repo,capture_output=True,text=True,encoding='utf-8')
            if addition.returncode!=1:raise ValueError('Cannot export upstream source addition')
            patch+=addition.stdout
        if 'GIT binary patch' in patch:raise ValueError('Binary upstream patch must be reviewed separately')
        target=public/'patches'/name;target.parent.mkdir(exist_ok=True);target.write_text(patch,encoding='utf-8',newline='\n')
        exported['patches/'+name]=sha256(target)
    for name in ('Petari','Aurora'):
        source=ROOT/('aurora' if name=='Aurora' else name)/'LICENSE'
        target=public/'licenses'/(name+'.txt');target.parent.mkdir(exist_ok=True)
        target.write_text(source.read_text(encoding='utf-8-sig'),encoding='utf-8',newline='\n')
        exported['licenses/'+target.name]=sha256(target)
    # Remove only files recorded by an earlier export, never arbitrary repository content.
    previous=public/'source-export.json'
    if previous.exists():
        for name in json.loads(previous.read_text())['files'].keys()-exported.keys():
            target=public/name
            if not target.resolve().is_relative_to(public.resolve()):raise ValueError('Unsafe old export path')
            if target.is_file():target.unlink()
    (public/'source-export.json').write_text(json.dumps({'files':exported},indent=2)+'\n')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--check-only',action='store_true');args=parser.parse_args()
    build=ROOT/'build-game'
    package=json.loads((build/'portable-package.json').read_text())
    proof=json.loads((build/'portable-final-verification.json').read_text())
    full=json.loads((build/'portable-full-regressions.json').read_text())
    game=sha256(build/'galaxy.exe');portable=sha256(build/'galaxy_portable.exe')
    if not (proof['exit']==0 and proof['gui_after_centering_exit']==0 and proof['empty_mods'] and proof['regular_save_unchanged']
            and full['exit']==0 and full['regular_save_unchanged'] and full['game_sha256']==game and full['portable_sha256']==portable
            and proof['portable_sha256']==portable and proof['full_regressions']==full and package['boot_assets_absent']):
        raise ValueError('Current executable must pass full, ROM-import and native-assets-only boot checks')
    compiled=[ROOT/'CMakeLists.txt',*(ROOT/'native').glob('*.cpp'),*(ROOT/'native').glob('*.hpp')]
    for repo in ('Petari','aurora'):
        names=run(['git','diff','--name-only'],ROOT/repo).splitlines()+run(['git','ls-files','--others','--exclude-standard'],ROOT/repo).splitlines()
        compiled.extend(ROOT/repo/name for name in names if Path(name).suffix in ('.cpp','.hpp','.c','.h','.cmake','.inc') and (ROOT/repo/name).is_file())
    if any(p.stat().st_mtime_ns>(build/'galaxy.exe').stat().st_mtime_ns for p in compiled):
        raise ValueError('Source changed after the verified build; rebuild and retest first')
    archive=Path(package['zip'])
    if not archive.resolve().is_relative_to((ROOT/'dist').resolve()):raise ValueError('Package outside dist')
    manifest=validate_package(archive,package['sha256'],portable)
    if args.check_only:print('Publishing checks passed; no remote writes.');return
    if json.loads((ROOT/'github-publishing.json').read_text())!={'repository':REPOSITORY,'enabled':True}:
        raise ValueError('Publishing must be explicitly enabled for this repository')
    gh=ROOT/'tools/github-cli/bin/gh.exe'
    if not gh.is_file():raise RuntimeError('Install and sign in to GitHub CLI before publishing')
    run([gh,'auth','status'])
    checkout=build/'github-publish'
    if not (checkout/'.git').exists():run([gh,'repo','clone',REPOSITORY,checkout])
    if run(['git','remote','get-url','origin'],checkout)!='https://github.com/'+REPOSITORY+'.git':raise ValueError('Wrong publishing remote')
    if run(['git','status','--porcelain'],checkout):raise ValueError('Publishing checkout has local edits; review before continuing')
    run(['git','pull','--ff-only','origin','main'],checkout)
    export_sources(checkout)
    for source,target in [('tools/github-readme.md','README.md'),('tools/github-ignore.txt','.gitignore')]:
        shutil.copyfile(ROOT/source,checkout/target)
    upstreams={name:{'url':run(['git','remote','get-url','origin'],ROOT/folder),
                     'commit':run(['git','rev-parse','HEAD'],ROOT/folder)} for name,folder in [('Petari','Petari'),('Aurora','aurora')]}
    (checkout/'native-client/upstreams.json').write_text(json.dumps(upstreams,indent=2)+'\n')
    summary={'full_regressions_passed':True,'full_regressions_seconds':full['seconds'],'development_executable_sha256':game,
             'portable_executable_sha256':portable,'package_sha256':package['sha256'],'package':archive.name,
             'native_assets_only_boot_verified':True,'rom_assets_included':False,'local_save_unchanged':True,
             'lan_lobby_available':manifest.get('lan_lobby_available',False),'co_op_gameplay_available':False,
             'separate_pc_radmin_verified':False,'clean_windows10_launch_verified':False}
    (checkout/'release-verification.json').write_text(json.dumps(summary,indent=2)+'\n')
    run(['git','add','--','README.md','.gitignore','native-client','release-verification.json'],checkout)
    if run(['git','diff','--cached','--name-only'],checkout):
        run(['git','-c','user.name=Galaxy build publisher','-c','user.email=90880120+Recoi1@users.noreply.github.com',
             'commit','-m','Publish verified Galaxy native source and release metadata'],checkout)
        run(['git','push','origin','main'],checkout)
    commit=run(['git','rev-parse','HEAD'],checkout)
    tag='experimental-'+portable[:12]
    notes=build/'github-release-notes.md'
    notes.write_text('# Experimental Galaxy PC builder\n\nExtract the entire ZIP and open galaxy.exe. Drag your own USA RMGE01 revision 0 ROM into its window. '
                     'All seven DLLs must remain beside the executable. Native game assets are extracted locally from the owner\'s ROM. '
                     'No ROM, game textures/models/audio, saves, HD packs or custom character assets are included.\n\n'
                     'Full local regression suite and independent ROM-import/boot checks passed with mods absent. '
                     'Native Radmin lobby discovery and portraits are available; shared-level co-op gameplay is unfinished.\n\n'
                     '**Known issue:** a silent startup failure has been reported on a second Windows 10 PC; cause is unresolved. '
                     'This is a prerelease, not a clean-machine compatibility certification.\n\n'
                     f'Portable SHA256: `{portable}`\n\nZIP SHA256: `{package["sha256"]}`\n\nSource commit: `{commit}`\n')
    existing=subprocess.run([str(gh),'api','repos/'+REPOSITORY+'/releases/tags/'+tag],capture_output=True,text=True)
    if existing.returncode==0:
        release=json.loads(existing.stdout)
        match=next((a for a in release['assets'] if a['name']==archive.name),None)
        if match is None:run([gh,'release','upload',tag,archive,'--repo',REPOSITORY])
        elif match.get('digest')!='sha256:'+package['sha256']:raise ValueError('Existing release asset digest differs; refusing overwrite')
        url=release['html_url']
    else:
        url=run([gh,'release','create',tag,archive,'--repo',REPOSITORY,'--target',commit,'--title','Galaxy PC experimental '+portable[:12],
                 '--prerelease','--latest=false','--notes-file',notes])
    record={'repository':REPOSITORY,'commit':commit,'tag':tag,'url':url,'package_sha256':package['sha256']}
    (build/'github-publish-result.json').write_text(json.dumps(record,indent=2)+'\n');print(url)

if __name__=='__main__':main()
