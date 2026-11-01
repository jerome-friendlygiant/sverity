// sverity-args: --top top
// expect-none: LNT-057
module child (input logic [7:0] din, output logic [3:0] dout);
  assign dout = din[3:0];
endmodule
module top(input logic [7:0] a, output logic [3:0] q);
  child u0(.din(a), .dout(q));      // fixed-width ports: slang's LNT-004 only
endmodule
