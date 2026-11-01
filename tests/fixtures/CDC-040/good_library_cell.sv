// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// SYNC2 is declared as a 2-stage synchroniser in cells.json, so the crossing through it is recognised (INFO with a trust model).
// Without the library the stub below is a plain wire and the crossing is an ERROR (see bad_library_cell_not_declared.sv).
// expect: CDC-040 INFO
// expect: CDC-003 INFO
// expect-none: CDC-003 ERROR
module SYNC2(input logic CLK, D, output logic Q);
  assign Q = D;   // simulation stub: no registers
endmodule
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  logic w;
  SYNC2 u_sync(.CLK(clkb), .D(a_q), .Q(w));
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= w;
endmodule
