// sverity-args: --top top
// The body is a simulation model; synthesis replaces the attributed module, so it is inventoried as unmodelled.
// expect: CST-005 WARNING 9
// expect-coverage: blackboxes INCOMPLETE
(* blackbox *) module macro(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule
module top(input logic clk, d, output logic q);
  macro u_macro(.clk, .d, .q);
endmodule
