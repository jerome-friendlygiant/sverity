// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/broken.json
// A malformed library entry is reported, never silently ignored; SYNC2 stays an ordinary module (the crossing is an ERROR).
// expect: CST-018 ERROR
// expect: CDC-003 ERROR
module SYNC2(input logic CLK, D, output logic Q);
  assign Q = D;
endmodule
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, w;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  SYNC2 u_sync(.CLK(clkb), .D(a_q), .Q(w));
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= w;
endmodule
