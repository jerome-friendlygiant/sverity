// sverity-args: --top top
// expect: CDC-002 WARNING
// expect-unresolved: CDC-002
module top(input logic d, output logic q);
  wire first, second;
  assign first = second;
  assign second = first;
  always_ff @(posedge first) q <= d;
endmodule
