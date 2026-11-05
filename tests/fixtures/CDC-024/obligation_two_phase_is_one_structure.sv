// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A two-phase handshake is built from two toggles: one structure, one functional-correctness obligation.
// expect: CDC-014 INFO
// expect: CDC-015 INFO
// expect-count: CDC-024 WARNING 1
module top(input logic clka, clkb, rst_n, start, output logic busy, output logic done);
  logic req, r1, r2, r3, ack, a1, a2, a3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) req <= 1'b0; else if (start && !(req ^ a2)) req <= ~req;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {r3, r2, r1} <= '0; else {r3, r2, r1} <= {r2, r1, req};
  assign done = r3 ^ r2;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) ack <= 1'b0; else if (done) ack <= ~ack;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {a3, a2, a1} <= '0; else {a3, a2, a1} <= {a2, a1, ack};
  assign busy = req ^ a3;
endmodule
