// sverity-args: --top top
// Lookalike of unknown_cycle_through_computed_index with constant indices: the loop is decided, so it stays an ERROR.
// expect: LNT-012 ERROR
module top(input logic a, output logic y);
  logic [1:0] v;
  assign v[0] = v[1] & a;
  assign v[1] = v[0] | a;
  assign y = v[0];
endmodule
