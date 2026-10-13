#include "shell/lockscreen/lockscreen_login_box.h"
#include "tests/test_check.h"

#include <cmath>

int main() {
  TEST_CHECK(lockscreen_login_box::kCompactInputHeight == 60.0F);

  const float compactPanelHeight = lockscreen_login_box::defaultPanelHeight(
      lockscreen_login_box::LayoutMode::Compact, /*showSessionButtons=*/false, /*showInfoExtras=*/false
  );
  TEST_CHECK(compactPanelHeight == lockscreen_login_box::minPanelHeight(
                                      lockscreen_login_box::LayoutMode::Compact, false, false
                                  ));

  const auto layout = lockscreen_login_box::panelContentLayout(compactPanelHeight, compactPanelHeight, false);
  TEST_CHECK(std::abs(layout.controlHeight - 60.0F) < 0.01F);
  TEST_CHECK(layout.inputWidth > 0.0F);

  return 0;
}
