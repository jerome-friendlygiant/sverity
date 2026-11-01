// sverity-args: --top top --waiver-file @DIR@/slang_waiver.toml
// slang's own waiver files would delete diagnostics: they are refused with a warning and findings stay.
// expect: CST-006 WARNING
// expect: LNT-002 ERROR
// expect-coverage: upstream-diagnostics INCOMPLETE
module top(output logic y);
  assign y = undefined_thing;
endmodule
