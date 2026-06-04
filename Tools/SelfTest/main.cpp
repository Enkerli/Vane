// Headless unit-test runner -- the AUTHORITATIVE pass/fail gate for CI and the
// startup check.  It compiles the test translation units in directly (see the
// VaneSelfTest target in CMakeLists.txt), so JUCE's static UnitTest registrations
// are not dead-stripped the way they are from the assembled plugin's shared-code
// static library.  Exit code == number of failing tests, so a single
//     VaneSelfTest ; echo $?
// is an unambiguous gate (0 = all pass) with no stderr grepping.
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>   // ScopedJuceInitialiser_GUI
#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // tests touch File / format singletons

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);          // collect every failure, do not break
    runner.runAllTests();

    int passes = 0, failures = 0;
    const int suites = runner.getNumResults();
    for (int i = 0; i < suites; ++i)
        if (auto* r = runner.getResult (i)) { passes += r->passes; failures += r->failures; }

    std::printf ("VANE_SELFTEST %s (%d passed, %d failed, %d suites)\n",
                 failures == 0 ? "PASS" : "FAIL", passes, failures, suites);
    std::fflush (stdout);
    return failures == 0 ? 0 : 1;
}
