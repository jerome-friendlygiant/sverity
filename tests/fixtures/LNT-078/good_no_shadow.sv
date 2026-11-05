// sverity-args: --top top
// expect-none: LNT-078
module top(input logic a, output logic y);
  logic value;
  always_comb value = a;
  assign y = value;
endmodule
