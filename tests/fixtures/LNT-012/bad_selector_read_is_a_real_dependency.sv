// sverity-args: --top top
// Lookalike of good_bits_in_constant_selector: the selector really reads the target at run time, through a computed index.
// expect: LNT-012 WARNING
module top(input logic [1:0] a, output logic [3:0] y);
  logic [3:0] d;
  always_comb d[d[0]] = a[0];
  assign y = d;
endmodule
