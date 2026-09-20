/* MODULE_DEF 로 선언한 모듈을 한 곳(.module)에 모은다. module.c 가 _smodule ~ _emodule 을 순회한다. */

SECTION_PROLOGUE(.module,,)
{
  . = ALIGN(4);
  _smodule = .;
  KEEP (*(.module))
  KEEP (*(.module*))
  _emodule = .;
} GROUP_LINK_IN(ROMABLE_REGION)
