// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The minimum-depth reciprocal handshake has one CDC-024 obligation; an unrelated opposite-direction synchronizer is not a second protocol.
// expect: CDC-015 INFO
// expect-count: CDC-024 WARNING 1
module top(input logic clka, clkb, rst_n, start, pulse, output logic busy, output logic observed);
  logic req, r1, r2, ack, a1, a2, independent, i1, i2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) req <= 0; else if (start && !req) req <= 1; else if (a2) req <= 0;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {r2,r1} <= 0; else {r2,r1} <= {r1,req};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) ack <= 0; else ack <= r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {a2,a1} <= 0; else {a2,a1} <= {a1,ack};
  always_ff @(posedge clkb) independent <= pulse;
  always_ff @(posedge clka) begin i1 <= independent; i2 <= i1; end
  assign observed = i2;
  assign busy = req;
endmodule
