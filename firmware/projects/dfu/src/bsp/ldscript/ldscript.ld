/* MODULE_DEF 로 선언한 모듈을 한 곳(.module)에 모은다. module.c 가 _smodule ~ _emodule 을 순회한다. */

SECTION_PROLOGUE(.module,,)
{
  . = ALIGN(4);
  _smodule = .;
  KEEP (*(.module))
  KEEP (*(.module*))
  _emodule = .;
} GROUP_LINK_IN(ROMABLE_REGION)

/* BLE_SVC_DEF 로 선언한 BLE 서비스를 한 곳(.ble_svc)에 모은다. ble.c 가 _sble_svc ~ _eble_svc 를 순회한다. */

SECTION_PROLOGUE(.ble_svc,,)
{
  . = ALIGN(4);
  _sble_svc = .;
  KEEP (*(.ble_svc))
  KEEP (*(.ble_svc*))
  _eble_svc = .;
} GROUP_LINK_IN(ROMABLE_REGION)
