"""Metadata-only discovery and explicit selection of local PaddleOCR ONNX pairs."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
from itertools import islice

MAX_PACKAGES=128
MAX_FILES=64
MAX_CONFIG_BYTES=65536
LOCAL_ID=re.compile(r'local-[a-f0-9]{64}\Z')
WEIGHT=re.compile(r'(?:(ch|multi|en)_)?PP-OCR(v[456])_(det|rec)_(mobile|server|tiny|small|medium)\.onnx\Z',re.I)
BUILTINS=(('builtin-ppocrv6-tiny','PP-OCRv6 tiny · 快速 · 6.0 MiB','PP-OCRv6','tiny','PP-OCRv6'),
          ('builtin-ppocrv5-mobile','PP-OCRv5 mobile · 默认 · 20.5 MiB','PP-OCRv5','mobile','ch_PP-OCRv5'),
          ('builtin-ppocrv4-mobile','PP-OCRv4 mobile · 轻量 · 14.9 MiB','PP-OCRv4','mobile','ch_PP-OCRv4'))

class CatalogError(Exception):
    def __init__(self,code,message):
        self.code=code;self.message=message
        super().__init__(message)

def fail(message,code='model_package_invalid'):
    raise CatalogError(code,message)

def integer(value,low,high):
    return isinstance(value,int) and not isinstance(value,bool) and low<=value<=high

def compatible(family,kind):
    return (family in ('PP-OCRv4','PP-OCRv5') and kind in ('mobile','server')) or (family=='PP-OCRv6' and kind in ('tiny','small','medium'))

def options(data):
    cpu=data.get('cpu',{'intraOpThreads':2,'interOpThreads':1})
    if not isinstance(cpu,dict) or set(cpu)-{'intraOpThreads','interOpThreads'}:fail('CPU 配置无效。')
    cpu={'intraOpThreads':cpu.get('intraOpThreads',2),'interOpThreads':cpu.get('interOpThreads',1)}
    if not integer(cpu['intraOpThreads'],1,8) or not integer(cpu['interOpThreads'],1,4):fail('CPU 线程超出支持范围。')
    detector=data.get('detector',{'limitType':'min','limitSideLen':736})
    if not isinstance(detector,dict) or set(detector)-{'limitType','limitSideLen'}:fail('检测尺寸配置无效。')
    detector={'limitType':detector.get('limitType','min'),'limitSideLen':detector.get('limitSideLen',736)}
    if detector['limitType'] not in ('min','max') or not integer(detector['limitSideLen'],32,8192):fail('检测尺寸超出支持范围。')
    return cpu,detector

def identity(relative):
    return 'local-'+hashlib.sha256(relative.replace('\\','/').casefold().encode('utf-8')).hexdigest()

def root_path(directory):
    if not isinstance(directory,str) or not directory or len(directory)>32768 or not Path(directory).is_absolute():
        fail('模型目录必须为绝对路径。','model_directory_invalid')
    root=Path(directory)
    if root.exists() and not root.is_dir():fail('模型目录不是文件夹。','model_directory_invalid')
    return root.resolve()

def child_file(folder,name):
    if not isinstance(name,str) or not name or Path(name).name!=name or name in ('.','..'):
        fail('模型路径必须指向当前模型文件夹内的文件。')
    path=folder/name
    if path.suffix.lower()!='.onnx' or not path.is_file():fail('缺少可读取的 ONNX 模型文件。')
    if path.is_symlink() or path.resolve().parent!=folder.resolve():fail('模型文件不能指向模型文件夹之外。')
    return path

def package(folder,relative):
    config=folder/'model.json';paths=[]
    try:
        files=list(islice(folder.iterdir(),MAX_FILES+1))
    except OSError:fail('无法读取模型文件夹。')
    if len(files)>MAX_FILES:fail('模型文件夹内容过多，请每个子文件夹只保留一组模型。')
    if config.exists():
        if config.is_symlink() or config.resolve().parent!=folder.resolve():fail('模型描述文件不能指向文件夹之外。')
        if config.stat().st_size>MAX_CONFIG_BYTES:fail('model.json 过大。')
        try:data=json.loads(config.read_text(encoding='utf-8-sig'))
        except (ValueError,OSError,RecursionError):fail('model.json 无法解析或嵌套过深，请修正后刷新。')
        if not isinstance(data,dict) or set(data)-{'schemaVersion','name','description','modelVersion','language','cpu','detector','det','rec'} or not integer(data.get('schemaVersion'),1,1):
            fail('model.json 版本或字段不受支持。')
        name=data.get('name',folder.name)
        if not isinstance(name,str) or not name.strip() or len(name)>128:fail('模型名称无效。')
        description=data.get('description','本地文件夹模型 · 保存时实际加载验证。')
        if not isinstance(description,str) or len(description)>1024:fail('模型说明过长或无效。')
        profile={'id':identity(relative),'modelVersion':data.get('modelVersion',name),'language':data.get('language','multi')}
        if profile['language'] not in ('multi','zh','en','zh+en'):fail('模型语言不受支持。')
        if not isinstance(profile['modelVersion'],str) or not profile['modelVersion'].strip() or len(profile['modelVersion'])>128:fail('模型版本标签无效。')
        for task in ('det','rec'):
            model=data.get(task)
            if not isinstance(model,dict) or set(model)-{'path','sha256','ocrVersion','modelType','language'}:fail('模型描述缺少检测/识别配置。')
            if not compatible(model.get('ocrVersion'),model.get('modelType')):fail('当前引擎不支持该模型家族或类型。')
            language=model.get('language','ch')
            if language not in ('ch','en','multi','zh'):fail('检测/识别语言配置不受支持。')
            path=child_file(folder,model.get('path'));paths.append(str(path))
            digest=model.get('sha256')
            if digest is not None and (not isinstance(digest,str) or not re.fullmatch(r'[a-fA-F0-9]{64}',digest)):fail('模型 SHA256 格式无效。')
            profile[task]={**model,'path':str(path),'language':'ch' if language=='zh' else language}
        profile['cpu'],profile['detector']=options(data)
        paths.append(str(config))
    else:
        matches={'det':[],'rec':[]}
        unknown=[]
        for path in files:
            if path.suffix.lower()!='.onnx':continue
            match=WEIGHT.fullmatch(path.name)
            if not match:unknown.append(path.name);continue
            lang,version,task,kind=match.groups();family='PP-OCR'+version.lower();kind=kind.lower();task=task.lower()
            if not compatible(family,kind):fail('当前引擎不支持文件名中的模型家族/类型。')
            path=child_file(folder,path.name)
            matches[task].append({'path':str(path),'ocrVersion':family,'modelType':kind,'language':(lang or 'ch').lower()})
        if unknown:fail('ONNX 文件名无法识别；请使用官方模型文件名，或提供 model.json。')
        if len(matches['det'])!=1 or len(matches['rec'])!=1:fail('请在此文件夹放置一份 det 检测模型和一份 rec 识别模型，避免重复或缺失。')
        det,rec=matches['det'][0],matches['rec'][0]
        if det['ocrVersion']!=rec['ocrVersion'] or det['modelType']!=rec['modelType']:
            fail('自动识别需要同系列同类型模型对；混合模型请提供 model.json。')
        family,kind=det['ocrVersion'],det['modelType']
        name=f'{folder.name} · {family} {kind}'
        description='本地文件夹模型 · 保存时实际加载验证。'
        profile={'id':identity(relative),'modelVersion':family+'_'+kind+'/local','language':'en' if rec['language']=='en' else 'multi','det':det,'rec':rec}
        if family=='PP-OCRv6':profile['cpu']={'intraOpThreads':4,'interOpThreads':1};profile['detector']={'limitType':'max','limitSideLen':960}
        else:profile['cpu'],profile['detector']=options({})
        paths=[str(det['path']),str(rec['path'])]
    return {'id':profile['id'],'name':name,'description':description,'enabled':True,'source':'folder','files':paths,'profile':profile}

def entries(directory):
    root=root_path(directory)
    if not root.exists():return []
    try:
        children=sorted(islice(root.iterdir(),MAX_PACKAGES*MAX_FILES+1),key=lambda p:p.name.casefold())
        if len(children)>MAX_PACKAGES*MAX_FILES:fail('模型目录内容过多。','model_directory_invalid')
        folders=[(root,'.')] if any(p.suffix.lower()=='.onnx' or p.name.lower()=='model.json' for p in children if p.is_file()) else []
        folders.extend((p,p.name) for p in children if p.is_dir())
        if len(folders)>MAX_PACKAGES:fail('模型数量超过 128 组，请减少模型后刷新。','model_directory_invalid')
        out=[]
        for folder,relative in folders:
            try:
                if folder.is_symlink() or (relative!='.' and folder.resolve().parent!=root):fail('模型子文件夹不能指向目录之外。')
                item=package(folder,relative)
            except (CatalogError,OSError) as error:
                message=error.message if isinstance(error,CatalogError) else '无法读取模型文件。'
                item={'id':identity(relative),'name':folder.name,'description':message,'enabled':False,'source':'folder','files':[],'error':message,'errorCode':'model_package_invalid'}
            out.append(item)
        return out
    except CatalogError:raise
    except OSError:fail('无法读取模型目录。','model_directory_invalid')

def catalog(directory,runtime):
    models=[]
    for identifier,name,family,kind,prefix in BUILTINS:
        files=[str(runtime/'models'/f'{prefix}_{task}_{kind}.onnx') for task in ('det','rec')]
        enabled=all(Path(p).is_file() for p in files)
        models.append({'id':identifier,'name':name,'description':'随应用提供 · 本地离线识别。' if enabled else '内置模型文件缺失，请完整解压发行包。','enabled':enabled,'source':'builtin','files':files})
    for item in entries(directory):models.append({k:v for k,v in item.items() if k!='profile'})
    return {'ok':True,'models':models}

def resolve(directory,identifier):
    if not isinstance(identifier,str) or not LOCAL_ID.fullmatch(identifier):fail('本地模型 ID 无效。','invalid_request')
    for item in entries(directory):
        if item['id']==identifier:
            if not item['enabled']:fail(item['error'])
            return item['profile']
    fail('所选本地模型已移走或重命名，请重新选择模型。','model_package_missing')
