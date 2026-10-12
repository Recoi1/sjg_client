"""Publication boundary checks use synthetic files, never ROM assets or GitHub writes."""
from pathlib import Path
import hashlib, importlib.util, json, tempfile, zipfile, warnings

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
print('Publication boundary checks passed; no GitHub writes.')
