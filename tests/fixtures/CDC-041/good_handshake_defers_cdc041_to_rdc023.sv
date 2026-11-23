// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Unpaired independent-reset handshake with clean synchroniser stages defers per-leg CDC-041 warnings to joint RDC-023.
// expect: RDC-023 WARNING
// expect-none: CDC-041
module top(input logic clka, clkb, rst_a_n, rst_b_n, start, output logic busy);
  logic req, r1, r2, ack, a1, a2;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) req <= 1'b0; else if (start & ~req) req <= 1'b1; else if (a2) req <= 1'b0;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) {r2, r1} <= '0; else {r2, r1} <= {r1, req};
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) ack <= 1'b0; else ack <= r2;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) {a2, a1} <= '0; else {a2, a1} <= {a1, ack};
  assign busy = req;
endmodule
