// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Combinational reset from domain clka drives both unconstrained clkc (Unknown) and async clkb (Async):
// the Async ERROR must not be masked by the Unknown WARNING.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, clkc, rst_n, en, d1, d2, output logic qc, qb);
  logic ra, g;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) ra <= 1'b0; else ra <= 1'b1;
  assign g = ra & en;
  always_ff @(posedge clkc or negedge g) if (!g) qc <= 1'b0; else qc <= d1;
  always_ff @(posedge clkb or negedge g) if (!g) qb <= 1'b0; else qb <= d2;
endmodule
