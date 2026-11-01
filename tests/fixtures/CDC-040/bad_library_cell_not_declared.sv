// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The same design without the library description: SYNC2 is an ordinary combinational wire, so the crossing is unsynchronised.
// expect: CDC-003 ERROR
// expect-none: CDC-040
module SYNC2(input logic CLK, D, output logic Q);
  assign Q = D;
endmodule
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  logic w;
  SYNC2 u_sync(.CLK(clkb), .D(a_q), .Q(w));
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= w;
endmodule
