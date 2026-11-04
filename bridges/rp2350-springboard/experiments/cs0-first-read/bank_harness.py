"""Bank-A layout control over the instruction-boundary register backend."""
import types
from instruction_harness import text,Runner
text=text.replace('TABLE=0x20060000','TABLE=0x20030000').replace('(UC_ARM_REG_R1, WORDS)','(UC_ARM_REG_R1, TABLE)')
m=types.ModuleType('bank_instruction_mapped');exec(compile(text,'bank_instruction_mapped','exec'),m.__dict__)
Original=m.Runner
class BankRunner(Original):
 def __init__(self,elf,library,mmio_backend='instruction'):
  assert mmio_backend=='instruction'
  super().__init__(elf,library,mmio_backend)
m.Runner=BankRunner
