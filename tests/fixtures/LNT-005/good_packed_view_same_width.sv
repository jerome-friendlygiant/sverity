// sverity-args: --top top
// The same 32 bits seen as bytes or as halves, 2-state into 4-state: synthesis builds the wires as written. INFO, not a hazard.
// expect: LNT-005 INFO 7
// expect: LNT-005 INFO 8
// expect-none: LNT-005 WARNING
module top(input logic [31:0] w, input bit [31:0] b, output logic [3:0][7:0] bytes, output logic [1:0][15:0] halves);
  assign bytes = w;
  assign halves = b;
endmodule
