// sverity-args: --top top
// The output-enable idiom: a ?: whose 'bz arm is wider than the net is narrowed as a whole; each arm is judged on the
// bits the net keeps, so an all-z arm stays the tri-state INFO and an arm that keeps a 0 bit is WARNING.
// expect: LNT-039 INFO 7
// expect: LNT-039 WARNING 8
module top(input logic oe, input logic [7:0] d8, input logic d1, output wire [7:0] b, output wire n);
  assign b = oe ? d8 : 'bz;
  assign n = oe ? d1 : 2'bz0;
endmodule
