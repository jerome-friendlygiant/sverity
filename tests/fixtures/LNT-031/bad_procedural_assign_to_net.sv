// sverity-args: --top top
// revert: wire w; => logic w;
// revert-expect-none: LNT-031
// A net cannot be assigned in a procedural context (slang error mapped to LNT-031).
// expect: LNT-031 ERROR
module top(input logic a, output logic y);
  wire w;
  always_comb w = a;
  assign y = w;
endmodule
