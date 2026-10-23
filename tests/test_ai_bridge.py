import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import backend_ai_bridge as bridge
import standalone_ai_provider as provider

RAW=b"1\t12345678-abcdef01-1\t0\t1\tteam\tteams\t1\tde_dust2\t100\t800\thello\n"
class BridgeTests(unittest.TestCase):
 def test_authentication_error_is_actionable_and_redacted(self):
  from urllib.error import HTTPError
  message=bridge.error_message(HTTPError('https://example.invalid/secret',401,'secret-key',{},None))
  self.assertIn('401',message)
  self.assertIn('--setup-key',message)
  self.assertNotIn('secret',message)
 def test_cost_limits_persist_and_cache_expires(self):
  with tempfile.TemporaryDirectory() as d:
   costs=bridge.CostGuard(Path(d),hourly_limit=20,daily_limit=100,min_interval=30)
   self.assertTrue(costs.reserve(1000))
   self.assertFalse(costs.reserve(1001))
   for i in range(1,20):self.assertTrue(costs.reserve(1000+i*31))
   self.assertFalse(costs.reserve(1620))
   costs.remember('key','hello',1000)
   costs.db.close()
   costs=bridge.CostGuard(Path(d),hourly_limit=20,daily_limit=100,min_interval=30)
   self.assertFalse(costs.reserve(1621))
   self.assertEqual(costs.cached('key',2000),'hello')
   self.assertIsNone(costs.cached('key',87401))
   costs.db.close()
 def test_default_has_no_paid_request_cap(self):
  with tempfile.TemporaryDirectory() as d:
   costs=bridge.CostGuard(Path(d))
   for i in range(150):self.assertTrue(costs.reserve(1000+i*.01))
   costs.db.close()
 def test_daily_cost_limit(self):
  with tempfile.TemporaryDirectory() as d:
   costs=bridge.CostGuard(Path(d),hourly_limit=20,daily_limit=100,min_interval=30)
   for i in range(100):self.assertTrue(costs.reserve(1000+i*800))
   self.assertFalse(costs.reserve(81000))
   self.assertTrue(costs.reserve(87401))
   costs.db.close()
 def test_request(self):
  token, event=bridge.parse_request(RAW)
  self.assertEqual(event["bot_slot"],0)
  self.assertTrue(event["bot_tags_on"])
  for raw in [RAW+b"x"*1025,RAW.replace(b"\t0\t1\t",b"\t32\t1\t"),RAW.replace(b"hello",b"he\0llo"),RAW.replace(b"team\t",b"bad\t")]:
   with self.assertRaises(ValueError):bridge.parse_request(raw)
 def test_reply(self):
  with tempfile.TemporaryDirectory() as d:
   for line in ['quit;exit','x\ny','x'*91,'%s']:
    with self.assertRaises(ValueError):bridge.publish(Path(d),'token',line)
   bridge.publish(Path(d),'token','hey')
   self.assertEqual((Path(d)/'reply.tsv').read_text(),'token\they\n')
 def test_normal_provider_variants(self):
  for content in ['{"reply":"hello there, how are you doing now.","action":"none"}',
                  '```json\n{"reply":"i’m here."}\n```', 'hello there.']:
   choice=provider.parse_model_content(content)
   provider.validate_model_choice(choice,{})
   with tempfile.TemporaryDirectory() as d:
    bridge.publish(Path(d),'token',choice['reply'])
  for content in ['{"reply":"a;quit"}', '{"reply":"hello","action":"follow"}',
                  json.dumps({'reply':'界'*31}), json.dumps({'reply':'x\ny'})]:
   with self.assertRaises(ValueError):provider.parse_model_content(content)
 def test_safe_chat_formatting(self):
  samples = {'"hey there"':'hey there', '“hello!”':'hello!', '**hey :)**':'hey :)',
             'hi 👋🏽':'hi', 'hey\u00a0there':'hey there', 'hello ❤️':'hello',
             "i’m ready (ct)":"i'm ready (ct)"}
  for content, expected in samples.items():
   with self.subTest(content=content):
    choice=provider.parse_model_content(content)
    self.assertEqual(choice['reply'],expected)
    with tempfile.TemporaryDirectory() as d:bridge.publish(Path(d),'token',choice['reply'])
  for content in ['👋', 'hello;quit', 'hello%s', 'hello\\quit', 'he\x00llo',
                  'hello\nquit', 'hello\u202equit', '{"reply":"hello']:
   with self.subTest(content=content):
    with self.assertRaises(ValueError):provider.parse_model_content(content)
 def test_question_scope(self):
  for text in ['greg google this', 'latest news please', 'weather tomorrow?', 'look up cs patch notes', 'current exchange rate', 'ano weather ngayon?']:
   self.assertIsNotNone(provider.local_scope_reply({'text':text}))
  for text in ['what is 2 plus 2?', 'how do i defuse?', 'how much is an ak47?', 'where is bombsite a?', 'name a pink fruit']:
   self.assertIsNone(provider.local_scope_reply({'text':text}))
 def test_specific_validation_diagnostics(self):
  self.assertIn('90 UTF-8 bytes',bridge.error_message(ValueError('invalid reply length')))
 def test_provider_validation(self):
  for choice in [{"reply":"hello","action":"follow"},{"reply":";quit","action":"none"},{"reply":"x"*91}]:
   with self.assertRaises(ValueError):provider.validate_model_choice(choice,{})
 def test_private_key_setup_and_environment_override(self):
  from unittest.mock import patch
  with tempfile.TemporaryDirectory() as d:
   credentials=Path(d)/'deepseek.env'
   with patch.object(sys,'argv',['bridge','--setup-key','--env-file',str(credentials)]), patch('getpass.getpass',return_value='test-secret'), patch('builtins.print'):
    bridge.main()
   self.assertEqual(credentials.read_text(),'DEEPSEEK_API_KEY=test-secret\n')
   self.assertEqual(credentials.stat().st_mode & 0o077,0)
   with patch.dict(os.environ,{'DEEPSEEK_API_KEY':'override','YAPB_AI_API_KEY':''}):
    self.assertEqual(provider.get_settings(credentials)['api_key'],'override')
 def test_standalone_end_to_end(self):
  received=[]
  class Handler(BaseHTTPRequestHandler):
   def log_message(self,*args):pass
   def do_POST(self):
    received.append(json.loads(self.rfile.read(int(self.headers['Content-Length']))))
    self.send_response(200);self.end_headers()
    self.wfile.write(json.dumps({'choices':[{'message':{'content':'{"reply":"hey there","action":"none"}'}}]}).encode())
  server=HTTPServer(('127.0.0.1',0),Handler)
  thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
  try:
   with tempfile.TemporaryDirectory() as d:
    queue=Path(d);(queue/'request.tsv').write_bytes(RAW)
    env=os.environ.copy();env['DEEPSEEK_API_KEY']='test-only';env.pop('YAPB_AI_API_KEY',None)
    env['YAPB_AI_API_URL']=f'http://127.0.0.1:{server.server_port}/chat';env['YAPB_AI_MODEL']='deepseek-flash'
    proc=subprocess.Popen([sys.executable,bridge.__file__,'--queue-dir',d],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
     deadline=time.monotonic()+5
     while not (queue/'reply.tsv').exists() and time.monotonic()<deadline:time.sleep(.05)
     self.assertEqual((queue/'reply.tsv').read_text(),'12345678-abcdef01-1\they there\n')
     self.assertEqual(received[0]['thinking'],{'type':'disabled'})
     self.assertEqual(received[0]['max_tokens'],32)
     prompt=received[0]['messages'][0]['content']
     self.assertIn('do not volunteer bot/AI identity',prompt)
     self.assertIn('sometimes politely disagree',prompt)
     self.assertIn('Never contradict known facts',prompt)
     self.assertIn('Human captain orders still win',prompt)
     self.assertIn('Only if directly asked',prompt)
     self.assertIn('nickname',prompt)
     self.assertIn('needing no lookup',prompt)
     self.assertIn('never guess',prompt)
     self.assertEqual(len(received),1)
     (queue/'reply.tsv').unlink()
     (queue/'request.tsv').write_bytes(RAW.replace(b'abcdef01-1',b'abcdef01-2'))
     deadline=time.monotonic()+3
     while not (queue/'reply.tsv').exists() and time.monotonic()<deadline:time.sleep(.05)
     self.assertEqual((queue/'reply.tsv').read_text(),'12345678-abcdef01-2\they there\n')
     self.assertEqual(len(received),1)
     (queue/'reply.tsv').unlink()
     (queue/'request.tsv').write_bytes(RAW.replace(b'abcdef01-1',b'abcdef01-3').replace(b'hello',b'google latest news'))
     deadline=time.monotonic()+3
     while not (queue/'reply.tsv').exists() and time.monotonic()<deadline:time.sleep(.05)
     self.assertIn("can't look that up",(queue/'reply.tsv').read_text())
     self.assertEqual(len(received),1)
    finally:
     proc.terminate();proc.communicate(timeout=3)
  finally:server.shutdown();server.server_close();thread.join()

class TemplateTests(unittest.TestCase):
 def event(self, player='keenplify', bot='will', subject=None):
  return dict(kind='chat', player_name=player, bot_name=bot, subject_name=subject or player,
              text='hi '+bot+'!', map='de_dust2', mode='teams', channel='all')
 def test_rebinds_subject_and_bot_across_cache(self):
  import reply_templates as t
  first=self.event()
  second=self.event('alex','greg')
  stored=t.to_template("hey keenplify! what's up!",first)
  self.assertEqual(stored,"hey {{s}}! what's up!")
  self.assertEqual(t.render(stored,second),"hey alex! what's up!")
  settings=dict(api_url='https://example.com',model='cheap')
  self.assertEqual(bridge.CostGuard.key(first,settings),bridge.CostGuard.key(second,settings))
 def test_actor_can_differ_from_interlocutor(self):
  import reply_templates as t
  event=self.event(subject='bob')
  self.assertEqual(t.to_template('bob blinded keenplify',event),'{{s}} blinded {{p}}')
  self.assertIn(t.render(t.event_template(dict(event,kind='team_flash')),event),[t.render(v,event) for v in t.event_variants(dict(event,kind='team_flash'))])
  self.assertIn(t.render(t.event_template(dict(event,kind='team_damage')),event),[t.render(v,event) for v in t.event_variants(dict(event,kind='team_damage'))])
 def test_names_are_replaced_once_at_word_boundaries(self):
  import reply_templates as t
  self.assertEqual(t.to_template('s greets p',self.event('s','p')),'{{s}} greets {{b}}')
  self.assertEqual(t.to_template('william, hi will!',self.event()),'william, hi {{b}}!')
 def test_injection_and_unbound_placeholders(self):
  import reply_templates as t
  for template in ['{{bad}}','{{s','{{p}}}']:
   with self.assertRaises(ValueError):t.render(template,self.event())
  with self.assertRaises(ValueError):t.to_template('hi {{s}}',self.event())
  for name in ['quit;exit','x\nquit','{{s}}','a'*64]:
   with self.assertRaises(ValueError):t.bindings(self.event(name))
 def test_v2_protocol(self):
  raw=RAW.replace(b'1\t',b'2\t',1).rstrip(b'\n')+b'\tteam_flash\tkeenplify\twill\tbob\n'
  _,event=bridge.parse_request(raw)
  self.assertEqual(event['subject_name'],'bob')
  self.assertEqual(event['kind'],'team_flash')

class DialogueVariantTests(unittest.TestCase):
 def test_context_fences_and_expiry(self):
  _,event=bridge.parse_request(RAW)
  event.update(kind='chat',context='1-20-30-1-1',bot_name='will',player_name='keen')
  memory=bridge.Conversation()
  memory.remember(event,'hey keen',100)
  self.assertEqual(len(memory.dialogue(event,101)),2)
  for changed in [dict(event,context='2-20-30-1-1'),dict(event,context='1-20-31-1-1'),dict(event,context='1-20-30-1-0'),dict(event,channel='all'),dict(event,player_name='other')]:
   self.assertEqual(memory.dialogue(changed,101),[])
  self.assertEqual(memory.dialogue(event,221),[])
 def test_history_is_bounded_and_not_persisted(self):
  _,event=bridge.parse_request(RAW);event.update(context='1-20-30-1-1')
  memory=bridge.Conversation()
  for n in range(10):memory.remember(event,'x'*90,100+n)
  history=memory.dialogue(event,111)
  self.assertEqual(len(history),4)
  self.assertTrue(all(len(m['content'])<=60 for m in history))
  self.assertEqual(bridge.Conversation().dialogue(event,111),[])
 def test_variants_do_not_repeat_when_multiple_exist(self):
  with tempfile.TemporaryDirectory() as d:
   cache=bridge.CostGuard(Path(d));cache.remember('k','hey {{s}}',100);cache.remember('k','hello {{s}}',101)
   last=cache.cached('k',102)
   for n in range(10):
    value=cache.cached('k',102+n);self.assertNotEqual(last,value);last=value
   cache.db.close()
 def test_v3_requires_round_life_identity_fence(self):
  raw=RAW.replace(b'1\t',b'3\t',1).rstrip(b'\n')+b'\tchat\tkeen\twill\tkeen\t2-20-30-1-1\n'
  _,event=bridge.parse_request(raw);self.assertEqual(event['context'],'2-20-30-1-1')
  with self.assertRaises(ValueError):bridge.parse_request(raw.replace(b'2-20-30-1-1',b'bad-context'))

class SharedCacheCompatibilityTests(unittest.TestCase):
 def test_existing_database_key_is_preserved_without_dialogue(self):
  import hashlib
  event=TemplateTests().event()
  settings=dict(api_url='https://api.deepseek.com/chat/completions',model='deepseek-flash')
  fields=['speech-v5-independent-opinions',settings['api_url'],settings['model'],'de_dust2','teams','all','chat','normal','hi {{b}}!']
  self.assertEqual(bridge.CostGuard.key(event,settings),hashlib.sha256('\0'.join(fields).encode()).hexdigest())
  contextual=dict(event,dialogue_hash='abc')
  self.assertNotEqual(bridge.CostGuard.key(contextual,settings),bridge.CostGuard.key(event,settings))
 def test_event_variants_preserve_site_and_count_facts(self):
  import reply_templates as templates
  for line in ['rotating to a','2 rotating b','3 a','low hp at b']:
   event=dict(TemplateTests().event(),kind='event',text=line)
   self.assertEqual(templates.event_variants(event),[line])

class WorkerOwnershipTests(unittest.TestCase):
 def test_busy_mailbox_and_ctrl_c_exit_without_tracebacks(self):
  import signal
  with tempfile.TemporaryDirectory() as directory:
   env={"PATH":os.environ.get("PATH",""),"DEEPSEEK_API_KEY":"test-only", "YAPB_AI_API_URL":"http://127.0.0.1:1/test"}
   command=[sys.executable,bridge.__file__,"--queue-dir",directory]
   owner=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
   try:
    deadline=time.monotonic()+3
    while not (Path(directory)/"ai_cost.sqlite3").exists() and time.monotonic()<deadline:time.sleep(.02)
    self.assertTrue((Path(directory)/"ai_cost.sqlite3").exists())
    duplicate=subprocess.run(command,env=env,capture_output=True,text=True,timeout=3)
    self.assertEqual(duplicate.returncode,75)
    self.assertEqual(duplicate.stdout,"")
    self.assertEqual(duplicate.stderr,"")
    owner.send_signal(signal.SIGINT)
    output,errors=owner.communicate(timeout=3)
    self.assertEqual(owner.returncode,0)
    self.assertIn("[ReGameDLL AI] ready",output)
    self.assertEqual(errors,"")
   finally:
    if owner.poll() is None:owner.kill();owner.communicate(timeout=3)

if __name__=='__main__':unittest.main()
