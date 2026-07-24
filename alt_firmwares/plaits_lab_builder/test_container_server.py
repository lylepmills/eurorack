from __future__ import annotations

import unittest

from container_server import classify_link_failure


class ClassifyLinkFailureTest(unittest.TestCase):
    def test_flash_overflow_reports_the_exact_overage(self) -> None:
        log = (
            "arm-none-eabi/bin/ld: <build-root>/plaits/plaits.elf "
            "section `.text' will not fit in region `FLASH'\n"
            "arm-none-eabi/bin/ld: region `FLASH' overflowed by 4096 bytes\n"
            "collect2: error: ld returned 1 exit status\n"
        )
        result = classify_link_failure(log)
        self.assertIsNotNone(result)
        code, message = result
        self.assertEqual(code, "flash_budget_exceeded")
        self.assertIn("4096 bytes", message)
        self.assertIn("flash", message.lower())

    def test_flash_overflow_without_a_byte_count_still_classifies(self) -> None:
        # Some ld invocations emit only the section "will not fit" line.
        log = "ld: section `.rodata' will not fit in region `FLASH'\n"
        code, message = classify_link_failure(log)
        self.assertEqual(code, "flash_budget_exceeded")
        self.assertNotIn("None", message)
        self.assertNotIn("bytes", message)  # no overage available -> no count

    def test_ram_overflow_is_classified_as_ram_not_compiler(self) -> None:
        log = (
            "ld: region `RAM' overflowed by 512 bytes\n"
            "ld: section `.bss' will not fit in region `RAM'\n"
        )
        code, message = classify_link_failure(log)
        self.assertEqual(code, "ram_budget_exceeded")
        self.assertIn("512 bytes", message)
        self.assertIn("RAM", message)

    def test_a_genuine_compiler_error_is_not_a_region_overflow(self) -> None:
        log = (
            "plaits/dsp/voice.cc:42:3: error: 'foo' was not declared in this scope\n"
            "make: *** [voice.o] Error 1\n"
        )
        self.assertIsNone(classify_link_failure(log))


if __name__ == "__main__":
    unittest.main()
