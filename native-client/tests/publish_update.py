"""Publication boundary checks use synthetic files, never ROM assets or GitHub writes."""
from pathlib import Path
import hashlib, importlib.util, json, tempfile, zipfile, warnings, sys, os

module=importlib.util.spec_from_file_location('publish_update',Path(__file__).resolve().parents[1]/'tools/publish-update.py')
publisher=importlib.util.module_from_spec(module);module.loader.exec_module(publisher)

with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'package.zip'
    files={name:b'synthetic program fixture' for name in {'galaxy.exe','README.txt',*publisher.DLLS}}
    files['licenses/fixture.txt']=b'synthetic license fixture'
    exe=hashlib.sha256(files['galaxy.exe']).hexdigest()
    def make(extra=None, bad_manifest=False, duplicate=False):
        manifest={'rom_assets_included':False,'files':{name:hashlib.sha256(data).hexdigest() for name,data in files.items()}}
        if bad_manifest:manifest['files']['galaxy.exe']='0'*64
        with warnings.catch_warnings():
            warnings.simplefilter('ignore',UserWarning)
            with zipfile.ZipFile(path,'w') as archive:
                for name,data in files.items():archive.writestr('Builder/'+name,data)
                archive.writestr('Builder/manifest.json',json.dumps(manifest))
                if extra:archive.writestr(extra,b'synthetic unapproved file')
                if duplicate:archive.writestr('Builder/galaxy.exe',files['galaxy.exe'])
        return publisher.sha256(path)
    assert publisher.validate_package(path,make(),exe)['rom_assets_included'] is False
    for extra,corrupt,duplicate in [('Builder/rom.wbfs',False,False),('Builder/../escape.txt',False,False),
                                    (None,True,False),(None,False,True)]:
        checksum=make(extra,corrupt,duplicate)
        try:publisher.validate_package(path,checksum,exe)
        except ValueError:pass
        else:raise AssertionError('Unsafe package accepted')
    make()
    for checksum,executable in [('0'*64,exe),(publisher.sha256(path),'0'*64)]:
        try:publisher.validate_package(path,checksum,executable)
        except ValueError:pass
        else:raise AssertionError('Changed package/executable accepted')

    sandbox=Path(directory)/'development';build=sandbox/'build-game';build.mkdir(parents=True)
    (sandbox/'native').mkdir();(sandbox/'CMakeLists.txt').write_text('synthetic CMake fixture')
    native=sandbox/'native/fixture.cpp';native.write_text('// synthetic source fixture')
    for name in ('galaxy.exe','galaxy_portable.exe'):(build/name).write_bytes(files['galaxy.exe'])
    dist=sandbox/'dist';dist.mkdir();package=dist/'Builder.zip';package.write_bytes(path.read_bytes())
    full={'exit':0,'regular_save_unchanged':True,'game_sha256':exe,'portable_sha256':exe}
    proof={'exit':0,'gui_after_centering_exit':0,'empty_mods':True,'regular_save_unchanged':True,'portable_sha256':exe,'full_regressions':full}
    def reports():
        (build/'portable-full-regressions.json').write_text(json.dumps(full))
        (build/'portable-final-verification.json').write_text(json.dumps(proof))
        (build/'portable-package.json').write_text(json.dumps({'zip':str(package),'sha256':publisher.sha256(package),'boot_assets_absent':True}))
    publisher.ROOT=sandbox
    # Git enumeration is empty in this isolated fixture; validation must never reach a network command.
    def fake_run(args,cwd=None):
        assert args[0]=='git' and args[1] in ('diff','ls-files'),args
        return ''
    publisher.run=fake_run;sys.argv=['publish-update.py','--check-only'];reports();publisher.main()
    for key,bad in [('exit',1),('empty_mods',False),('regular_save_unchanged',False),('portable_sha256','0'*64)]:
        previous=proof[key];proof[key]=bad;reports()
        try:publisher.main()
        except ValueError:pass
        else:raise AssertionError('Failed verification accepted: '+key)
        proof[key]=previous
    reports();os.utime(native,ns=((build/'galaxy.exe').stat().st_mtime_ns+1_000_000_000,)*2)
    try:publisher.main()
    except ValueError:pass
    else:raise AssertionError('Unbuilt source change accepted')
print('Publication boundary checks passed; no GitHub writes.')
