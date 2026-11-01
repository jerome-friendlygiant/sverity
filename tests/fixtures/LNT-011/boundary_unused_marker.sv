// sverity-args: --top top
// A deliberate-unused marker silences slang, so SVerity records the marker as INFO (the suppression is visible).
// expect: LNT-011 INFO
module top(input logic a, output logic y);
  (* unused *) logic spare;
  assign y = a;
endmodule
