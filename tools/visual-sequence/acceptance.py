"""Real paired Luau effects, node breakpoints, state-preserving replacement and EOF."""
import copy
import json
from pathlib import Path
import sys
import tempfile

import cook
import model
from server import Native

ROOT=Path(__file__).resolve().parents[2]
DOC=model.parse(Path(__file__).with_name('fixtures').joinpath('encounter.json').read_text())


def require(ok, reason):
    if not ok: raise AssertionError(reason)


def debug(native, current, action, **extra):
    context=current['runtime']
    request={'version':1,'action':action,**{k:context[k] for k in ('session','execution','revision','stop')},**extra}
    return native.request({'action':'debug','request':json.dumps(request)})


def journey(executable,cache):
    artifact,maps=cook.compile_document(DOC,1,cache)
    condition=next(n['id'] for n in DOC['nodes'] if n['kind']=='If')
    increment=next(n['id'] for n in DOC['nodes'] if n['kind']=='Increment')
    changed=model.apply(DOC,{'kind':'literal','node':condition,'value':3})
    candidate, candidate_maps=cook.compile_document(changed,2,cache)
    native=Native(executable)
    try:
        current=native.request({'action':'load','artifact':artifact,'expected':''})
        require(current['ok'],'trusted graph initial load')
        current=native.request({'action':'interact','instance':0,'amount':1})
        require(current['ok'] and current['runtime']['states'][0]['interactions']==1 and not current['open'][0],'first interaction')
        line=next(s['line'] for s in maps['spans'] if s['node']==condition)
        current=debug(native,current,'breakpoint',asset='0000000000000100',line=line,enabled=True)
        require(current['ok'],'node breakpoint resolves')
        current=native.request({'action':'interact','instance':0,'amount':1})
        require(current['ok'] and current['runtime']['partial'] and current['runtime']['states'][0]['interactions']==1,'paused mutation unpublished')
        frame=current['runtime']['frames'][0]
        require(any(s['compiled_line']==frame['compiled_line'] and s['node']==condition for s in maps['spans']),'node source map')
        frozen=copy.deepcopy(current['runtime'])
        current=native.request({'action':'load','artifact':candidate,'expected':artifact['key']})
        require(not current['ok'] and current['runtime']['tick']==frozen['tick'] and current['runtime']['package']==artifact['key'],'partial-tick replacement rejection')
        current=debug(native,current,'over')
        require(current['ok'] and current['runtime']['partial'],'real step over')
        require(any(s['compiled_line']==current['runtime']['frames'][0]['compiled_line'] for s in maps['spans']),'step source map')
        current=debug(native,current,'continue')
        # One breakpoint on the If is skipped once on continuation, then retires.
        require(current['ok'] and not current['runtime']['partial'] and current['open'][0] and current['runtime']['states'][0]['interactions']==2,'successful publication')
        stale=debug(native,current,'inspect')['runtime']
        current=native.request({'action':'load','artifact':candidate,'expected':artifact['key']})
        require(current['ok'] and current['runtime']['revision']==2 and current['runtime']['states'][0]['interactions']==2 and current['open'][0],'state-preserving whole-VM replacement')
        bad=copy.deepcopy(candidate);bad['pin']='0'*64;bad['revision']=3
        rejected=native.request({'action':'load','artifact':bad,'expected':candidate['key']})
        require(not rejected['ok'] and rejected['runtime']['package']==candidate['key'],'failed acquisition retains active')
        request={'version':1,'action':'inspect',**{k:stale[k] for k in ('session','execution','revision','stop')}}
        require(not native.request({'action':'debug','request':json.dumps(request)})['ok'],'stale source revision rejected')
        for count in (1,2,3):
            current=native.request({'action':'interact','instance':1,'amount':1})
            require(current['ok'] and current['runtime']['states'][1]['interactions']==count and current['open'][1]==(count==3),'replacement changes behavior')
        # Candidate allocation failure retires only the staging bank.
        oom,_=cook.compile_document(DOC,3,cache)
        require(native.request({'action':'fail_candidate','attempt':1})['ok'],'candidate failure hook')
        failed=native.request({'action':'load','artifact':oom,'expected':candidate['key']})
        require(not failed['ok'] and failed['runtime']['package']==candidate['key'] and failed['runtime']['states']==current['runtime']['states'],'OOM candidate retention')
        require(native.request({'action':'fail_candidate','attempt':0})['ok'],'clear failure hook')
        # Every generation owns fresh bytecode storage; repeated bank reuse is safe.
        for revision in range(3,19):
            doc=model.apply(DOC,{'kind':'literal','node':condition,'value':2+revision%2})
            artifact,_=cook.compile_document(doc,revision,cache)
            current=native.request({'action':'load','artifact':artifact,'expected':current['runtime']['package']})
            require(current['ok'] and current['runtime']['states'][1]['interactions']==3,'repeated replacement')
        result=native.request({'action':'close'})
        require(result['ok'] and result['heap']==0,'zero VM bytes on retirement')
    finally:native.close()

    # A separately authored text module uses the same operation/state contract.
    text=Path(__file__).with_name('fixtures').joinpath('reference.luau').read_text()
    text_artifact,_=cook.compile_source(text,[],DOC['graph'],cook.sha(text.encode()),1,cache)
    graph_artifact,_=cook.compile_document(DOC,1,cache)
    journals=[]
    for package in (graph_artifact,text_artifact):
        process=Native(executable)
        try:
            require(process.request({'action':'load','artifact':package,'expected':''})['ok'],'equivalence load')
            journal=[]
            for instance,amount in ((0,1),(1,1),(0,1),(0,1),(1,1)):
                result=process.request({'action':'interact','instance':instance,'amount':amount})
                require(result['ok'],'equivalent invocation')
                journal.append((result['runtime']['states'],result['open'],result['runtime']['tick'],result['runtime']['native_status'],result['effects']))
            journals.append(journal)
        finally:process.close()
    require(journals[0]==journals[1],'graph/text effect and ordering equivalence')
    # Bounded block lowering must execute its explicit child order, including an
    # early return before later siblings. No C++/JS graph evaluator is involved.
    blocks=model.apply(DOC,{'kind':'add','parent':DOC['root'],'type':'Repeat'})
    repeat=next(n['id'] for n in blocks['nodes'] if n['kind']=='Repeat')
    blocks=model.apply(blocks,{'kind':'add','parent':repeat,'type':'Return'})
    children=next(n['children'] for n in blocks['nodes'] if n['id']==DOC['root'])
    blocks=model.apply(blocks,{'kind':'order','node':DOC['root'],'children':[repeat]+[c for c in children if c!=repeat]})
    package,_=cook.compile_document(blocks,1,cache)
    process=Native(executable)
    try:
        require(process.request({'action':'load','artifact':package,'expected':''})['ok'],'structured repeat load')
        result=process.request({'action':'interact','instance':0,'amount':1})
        require(result['ok'] and result['runtime']['states'][0]['interactions']==0 and not result['effects'],'bounded repeat early return')
    finally:process.close()

    # Disconnect while stopped cancels unpublished effects and destroys the VM.
    process=Native(executable)
    try:
        current=process.request({'action':'load','artifact':graph_artifact,'expected':''})
        line=next(s['line'] for s in maps['spans'] if s['node']==increment)
        current=debug(process,current,'breakpoint',asset='0000000000000100',line=line,enabled=True)
        require(process.request({'action':'interact','instance':0,'amount':1})['runtime']['partial'],'disconnect stop')
        process.process.stdin.close()
        require(process.process.wait(timeout=5)==0,'EOF retirement')
    finally:process.close()


if __name__=='__main__':
    with tempfile.TemporaryDirectory() as temporary:journey(Path(sys.argv[1]),Path(temporary))
    print('S3 real Luau graph/text, node debugger, replacement and retirement PASS')
