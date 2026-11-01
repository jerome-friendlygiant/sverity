// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: ack <= r2; => ack <= 1'b0;
// revert-expect-none: CDC-024 WARNING
// A recognised handshake always carries the functional-correctness obligation (never reported as proven).
// expect: CDC-024 WARNING
module top(input logic clka, clkb, rst_n, start, output logic busy);
  logic req, r1, r2, ack, a1, a2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) req <= 1'b0; else if (start & ~req) req <= 1'b1; else if (a2) req <= 1'b0;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {r2, r1} <= '0; else {r2, r1} <= {r1, req};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) ack <= 1'b0; else ack <= r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {a2, a1} <= '0; else {a2, a1} <= {a1, ack};
  assign busy = req;
endmodule
