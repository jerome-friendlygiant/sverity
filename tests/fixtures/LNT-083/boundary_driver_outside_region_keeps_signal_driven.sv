// sverity-args: --top top
// `t` keeps its driver outside the region, so it is driven for synthesis; the region's own assignment of `y2` is reported.
// expect: LNT-083 WARNING 9
// expect: LNT-024 WARNING 6
// expect-none: LNT-008
module top(input logic a, b, output logic y, output logic y2);
  wire t;
  assign t = a;
  // synopsys translate_off
  assign y2 = b;
  // synopsys translate_on
  assign y = t;
endmodule
