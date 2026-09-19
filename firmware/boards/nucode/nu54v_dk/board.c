/*
 * NU54-DK 보드 초기화
 */

#include <zephyr/init.h>
#include <hal/nrf_nfct.h>


static int board_nu54v_dk_init(void)
{
  // NFC 안테나 없음 : P1.02/P1.03(NFC1/NFC2)은 I2C(Qwiic, PMIC)로 사용한다.
  // nRF54L15 NFCT.PADCONFIG 리셋값은 NFC 패드 활성 → GPIO 로 쓰려면 꺼야 한다.
  //
  nrf_nfct_pad_config_enable_set(NRF_NFCT, false);

  return 0;
}

SYS_INIT(board_nu54v_dk_init, PRE_KERNEL_1, 0);
