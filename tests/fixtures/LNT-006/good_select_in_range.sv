// sverity-args: --top top
// Near-miss of bad_select_out_of_range: the constant select is the highest valid index (3 of [3:0]).
// expect-none: LNT-006
module top(input logic [3:0] v, output logic b);
  assign b = v[3];
endmodule
