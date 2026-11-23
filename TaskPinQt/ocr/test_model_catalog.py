"""Filename-only local model package behavior; no cloud or user data."""
import argparse
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--runtime',required=True,type=Path)
    parser.add_argument('--small-model-dir',type=Path)
    parser.add_argument('--font',type=Path)
    args=parser.parse_args();runtime=args.runtime.resolve()
    env=os.environ.copy();env['PATH']=str(Path(os.environ.get('SystemRoot','C:/Windows'))/'System32')
    env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None)
    def invoke(arguments,request=None):
        process=subprocess.run([str(runtime/'python.exe'),'-I','-B',str(runtime/'helper.py'),*arguments],
            input=json.dumps(request).encode() if request is not None else None,capture_output=True,cwd=runtime,env=env,
            timeout=90,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        assert not process.stderr,process.stderr
        result=json.loads(process.stdout)
        assert bool(result['ok'])==(process.returncode==0),result
        return result
    def image():
        img=Image.new('RGB',(800,110),'white');font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',30)
        ImageDraw.Draw(img).text((24,28),'Hello TaskPin 2026',font=font,fill='black')
        buffer=io.BytesIO();img.save(buffer,format='PNG')
        return base64.b64encode(buffer.getvalue()).decode()
    png=image()
    with tempfile.TemporaryDirectory(prefix='taskpin-model-catalog-') as directory:
        root=Path(directory);folder=root/'新模型';folder.mkdir()
        for task in ('det','rec'):
            shutil.copyfile(runtime/'models'/f'ch_PP-OCRv5_{task}_mobile.onnx',folder/f'ch_PP-OCRv5_{task}_mobile.onnx')
        catalog=invoke(['--catalog','--model-dir',str(root)])
        assert catalog['ok'],catalog
        builtins={entry['id'] for entry in catalog['models'] if entry['source']=='builtin'}
        assert builtins=={'builtin-ppocrv4-mobile','builtin-ppocrv5-mobile','builtin-ppocrv6-tiny'},builtins
        entry=next(entry for entry in catalog['models'] if entry['source']=='folder')
        assert entry['enabled'] and entry['id'].startswith('local-') and len(entry['id'])==70,entry
        local_id=entry['id'];request={'image':png,'profileId':local_id,'modelDirectory':str(root)}
        recognized=invoke([],request)
        assert recognized['ok'] and recognized['profileId']==local_id and recognized['engine']=='PaddleOCR PP-OCRv5',recognized
        assert 'HelloTaskPin2026' in ''.join(line['text'] for line in recognized['lines']).replace(' ','')
        again=invoke(['--catalog','--model-dir',str(root)])
        assert any(e['id']==local_id and e['enabled'] for e in again['models'])
        (folder/'ch_PP-OCRv5_rec_mobile.onnx').rename(folder/'unknown.onnx')
        invalid=invoke(['--catalog','--model-dir',str(root)])
        assert not next(e for e in invalid['models'] if e['id']==local_id)['enabled']
        assert not invoke([],request)['ok']
        (folder/'unknown.onnx').rename(folder/'ch_PP-OCRv5_rec_mobile.onnx')
        shutil.copyfile(folder/'ch_PP-OCRv5_rec_mobile.onnx',folder/'en_PP-OCRv5_rec_mobile.onnx')
        invalid=invoke(['--catalog','--model-dir',str(root)])
        assert not next(e for e in invalid['models'] if e['id']==local_id)['enabled']
        (folder/'en_PP-OCRv5_rec_mobile.onnx').unlink()
        # Descriptor mistakes never silently revert to filename inference.
        (folder/'model.json').write_text('{broken',encoding='utf-8')
        assert not next(e for e in invoke(['--catalog','--model-dir',str(root)])['models'] if e['id']==local_id)['enabled']
        (folder/'model.json').unlink()
        # Exact bytes supplied in descriptor are optional, but declared hashes are enforced.
        descriptor={'schemaVersion':1,'name':'Local test','det':{'path':'ch_PP-OCRv5_det_mobile.onnx','ocrVersion':'PP-OCRv5','modelType':'mobile','sha256':'0'*64},
            'rec':{'path':'ch_PP-OCRv5_rec_mobile.onnx','ocrVersion':'PP-OCRv5','modelType':'mobile'}}
        (folder/'model.json').write_text(json.dumps(descriptor),encoding='utf-8')
        assert invoke([],request).get('errorCode')=='model_hash_mismatch'
        (folder/'model.json').unlink()
        # Unknown new family and generic names need an explicit compatible descriptor.
        unsupported=root/'future';unsupported.mkdir();(unsupported/'PP-OCRv99_det_tiny.onnx').write_bytes(b'x');(unsupported/'PP-OCRv99_rec_tiny.onnx').write_bytes(b'x')
        assert not next(e for e in invoke(['--catalog','--model-dir',str(root)])['models'] if e['source']=='folder' and 'future' in e['name'])['enabled']
        nested=root/'invalid-nested';nested.mkdir()
        (nested/'model.json').write_text('{"x":'+'['*12000+'0'+']'*12000+'}',encoding='utf-8')
        nested_catalog=invoke(['--catalog','--model-dir',str(root)])
        assert nested_catalog['ok'] and next(e for e in nested_catalog['models'] if e['id']==local_id)['enabled']
        assert not next(e for e in nested_catalog['models'] if 'invalid-nested' in e['name'])['enabled']
        shutil.rmtree(folder)
        assert invoke([],request).get('errorCode')=='model_package_missing'
        # Actual v6 tiny filename pair added after the application was built.
        folder=root/'v6-added';folder.mkdir()
        for task in ('det','rec'):
            shutil.copyfile(runtime/'models'/f'PP-OCRv6_{task}_tiny.onnx',folder/f'PP-OCRv6_{task}_tiny.onnx')
        entry=next(e for e in invoke(['--catalog','--model-dir',str(root)])['models'] if e['source']=='folder' and e['enabled'])
        recognized=invoke([],{'image':png,'profileId':entry['id'],'modelDirectory':str(root)})
        assert recognized['ok'] and recognized['engine']=='PaddleOCR PP-OCRv6' and recognized['profileId']==entry['id']
        assert 'HelloTaskPin2026' in ''.join(line['text'] for line in recognized['lines']).replace(' ','')
        font_path=args.font or Path('C:/Windows/Fonts/msyh.ttc')
        if not font_path.is_file():raise AssertionError('CJK fixture font required: pass --font')
        for text,dark in [('请确认翻译结果',False),('离线文字识别',True)]:
            img=Image.new('RGB',(800,110),'#171b22' if dark else 'white')
            font=ImageFont.truetype(str(font_path),30)
            ImageDraw.Draw(img).text((24,28),text,font=font,fill='white' if dark else 'black')
            buffer=io.BytesIO();img.save(buffer,format='PNG')
            result=invoke([],{'image':base64.b64encode(buffer.getvalue()).decode(),'profileId':entry['id'],'modelDirectory':str(root)})
            assert result['ok'] and text in ''.join(line['text'] for line in result['lines']).replace(' ','')
        # Replace package in place without changing its stable ID; identity and engine must change.
        old_identity=recognized['modelIdentity']
        for file in folder.glob('*.onnx'):file.unlink()
        for task in ('det','rec'):
            shutil.copyfile(runtime/'models'/f'ch_PP-OCRv4_{task}_mobile.onnx',folder/f'ch_PP-OCRv4_{task}_mobile.onnx')
        changed=invoke([],{'image':png,'profileId':entry['id'],'modelDirectory':str(root)})
        assert changed['ok'] and changed['profileId']==entry['id'] and changed['engine']=='PaddleOCR PP-OCRv4'
        assert changed['modelIdentity']!=old_identity
        small_test=False
        if args.small_model_dir:
            small=root/'small-added';small.mkdir()
            for task in ('det','rec'):
                shutil.copyfile(args.small_model_dir/f'multi_PP-OCRv6_{task}_small.onnx',small/f'PP-OCRv6_{task}_small.onnx')
            item=next(e for e in invoke(['--catalog','--model-dir',str(root)])['models'] if e['source']=='folder' and 'small-added' in e['name'])
            result=invoke([],{'image':png,'profileId':item['id'],'modelDirectory':str(root)})
            assert result['ok'] and result['engine']=='PaddleOCR PP-OCRv6' and result['profileId']==item['id']
            small_test=True
        for invalid in ({'profileId':'local-../escape','modelDirectory':str(root)},
                        {'profileId':'builtin-ppocrv6-tiny','modelDirectory':str(root)},
                        {'profileId':entry['id'],'modelDirectory':str(root),'profilePath':'x'},
                        {'profileId':entry['id'],'modelDirectory':'relative/root'}):
            assert not invoke([],{'image':png,**invalid})['ok']
    print(json.dumps({'ok':True,'filenameOnlyDiscovery':True,'newProcessDiscovery':True,'realDroppedModelRecognition':True,
        'removedModelRejected':True,'missingAndAmbiguousPairRejected':True,'malformedDescriptorRejected':True,
        'declaredHashEnforced':True,'replacementChangesIdentity':True,'realV6SmallDroppedAndLoaded':small_test,
        'malformedPackageDoesNotSuppressOtherPackages':True,'droppedChineseAndDarkTextRecognized':True,
        'noPaidService':True,'noUserData':True},indent=2))

if __name__=='__main__':main()
